#include "stamped_frame_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running(true);

struct Options {
  std::string source_host = "127.0.0.1";
  uint16_t source_port = 9002;
  uint16_t listen_port = 9102;
  std::string record_path;
  uint32_t max_raw_frame_size = 20u * 1024u * 1024u;
};

void on_signal(int) {
  g_running.store(false);
}

void usage(const char *program) {
  printf(
      "Usage: %s [options]\n"
      "Options:\n"
      "  --source-host <host>       Original TCP source host, default 127.0.0.1\n"
      "  --source-port <port>       Original TCP source port, default 9002\n"
      "  --listen-port <port>       Stamped forward listen port, default 9102\n"
      "  --record-path <file>       Optional stamped raw-frame output file\n"
      "  --max-frame-size <bytes>   Max raw frame size, default 20971520\n"
      "  --help                     Show this help\n",
      program);
}

bool parse_u16(const char *text, uint16_t *value) {
  char *end = nullptr;
  const long parsed = strtol(text, &end, 10);
  if (!end || *end != '\0' || parsed <= 0 || parsed > 65535) {
    return false;
  }
  *value = static_cast<uint16_t>(parsed);
  return true;
}

bool parse_u32(const char *text, uint32_t *value) {
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (!end || *end != '\0' || parsed == 0 || parsed > 0xfffffffful) {
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

bool parse_options(int argc, char **argv, Options *options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need_value = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s requires a value\n", name);
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--source-host") {
      const char *value = need_value("--source-host");
      if (!value) return false;
      options->source_host = value;
    } else if (arg == "--source-port") {
      const char *value = need_value("--source-port");
      if (!value || !parse_u16(value, &options->source_port)) return false;
    } else if (arg == "--listen-port") {
      const char *value = need_value("--listen-port");
      if (!value || !parse_u16(value, &options->listen_port)) return false;
    } else if (arg == "--record-path") {
      const char *value = need_value("--record-path");
      if (!value) return false;
      options->record_path = value;
    } else if (arg == "--max-frame-size") {
      const char *value = need_value("--max-frame-size");
      if (!value || !parse_u32(value, &options->max_raw_frame_size)) return false;
    } else if (arg == "--help") {
      usage(argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "Unknown option: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

uint64_t clock_ns(clockid_t clock_id) {
  timespec ts;
  if (clock_gettime(clock_id, &ts) != 0) {
    return 0;
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

double ns_to_ms(uint64_t ns) {
  return static_cast<double>(ns) / 1000000.0;
}

struct ForwardStats {
  uint64_t frames = 0;
  uint64_t bytes = 0;
  uint64_t window_start_ns = 0;
  uint64_t read_ns = 0;
  uint64_t stamp_crc_ns = 0;
  uint64_t write_ns = 0;
  uint64_t record_ns = 0;
  uint64_t active_ns = 0;
  uint64_t total_ns = 0;
  uint64_t max_active_ns = 0;
  uint64_t max_total_ns = 0;
  uint32_t last_raw_type = 0;
  uint32_t last_raw_frame_size = 0;

  void reset(uint64_t now_ns) {
    frames = 0;
    bytes = 0;
    window_start_ns = now_ns;
    read_ns = 0;
    stamp_crc_ns = 0;
    write_ns = 0;
    record_ns = 0;
    active_ns = 0;
    total_ns = 0;
    max_active_ns = 0;
    max_total_ns = 0;
    last_raw_type = 0;
    last_raw_frame_size = 0;
  }

  void add(uint64_t read_duration_ns,
           uint64_t stamp_crc_duration_ns,
           uint64_t write_duration_ns,
           uint64_t record_duration_ns,
           uint64_t active_duration_ns,
           uint64_t total_duration_ns,
           uint64_t frame_bytes,
           uint32_t raw_type,
           uint32_t raw_frame_size) {
    ++frames;
    bytes += frame_bytes;
    read_ns += read_duration_ns;
    stamp_crc_ns += stamp_crc_duration_ns;
    write_ns += write_duration_ns;
    record_ns += record_duration_ns;
    active_ns += active_duration_ns;
    total_ns += total_duration_ns;
    if (active_duration_ns > max_active_ns) {
      max_active_ns = active_duration_ns;
    }
    if (total_duration_ns > max_total_ns) {
      max_total_ns = total_duration_ns;
    }
    last_raw_type = raw_type;
    last_raw_frame_size = raw_frame_size;
  }

  void print(uint64_t now_ns, uint64_t total_frame_count) const {
    if (frames == 0) {
      return;
    }

    const double elapsed_s =
        static_cast<double>(now_ns - window_start_ns) / 1000000000.0;
    const double fps = elapsed_s > 0.0 ? static_cast<double>(frames) / elapsed_s : 0.0;
    const double mib_s =
        elapsed_s > 0.0 ? static_cast<double>(bytes) / (1024.0 * 1024.0) / elapsed_s : 0.0;
    const double frame_divisor = static_cast<double>(frames);
    printf("Forwarded %llu frames | window=%llu fps=%.2f throughput=%.2f MiB/s "
           "avg_ms active=%.3f total=%.3f read_wait=%.3f stamp_crc=%.3f write=%.3f record=%.3f "
           "max_active=%.3f max_total=%.3f last type=0x%04x size=%u\n",
           static_cast<unsigned long long>(total_frame_count),
           static_cast<unsigned long long>(frames),
           fps,
           mib_s,
           ns_to_ms(active_ns) / frame_divisor,
           ns_to_ms(total_ns) / frame_divisor,
           ns_to_ms(read_ns) / frame_divisor,
           ns_to_ms(stamp_crc_ns) / frame_divisor,
           ns_to_ms(write_ns) / frame_divisor,
           ns_to_ms(record_ns) / frame_divisor,
           ns_to_ms(max_active_ns),
           ns_to_ms(max_total_ns),
           static_cast<unsigned int>(last_raw_type),
           static_cast<unsigned int>(last_raw_frame_size));
    fflush(stdout);
  }
};

bool read_exact(int fd, void *buffer, size_t size) {
  uint8_t *cursor = static_cast<uint8_t *>(buffer);
  size_t remaining = size;
  while (remaining > 0 && g_running.load()) {
    const ssize_t n = recv(fd, cursor, remaining, 0);
    if (n == 0) {
      return false;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    cursor += n;
    remaining -= static_cast<size_t>(n);
  }
  return remaining == 0;
}

bool write_exact(int fd, const void *buffer, size_t size) {
  const uint8_t *cursor = static_cast<const uint8_t *>(buffer);
  size_t remaining = size;
  while (remaining > 0 && g_running.load()) {
    const ssize_t n = send(fd, cursor, remaining, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    cursor += n;
    remaining -= static_cast<size_t>(n);
  }
  return remaining == 0;
}

int connect_tcp(const std::string &host, uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    fprintf(stderr, "Invalid source host: %s\n", host.c_str());
    close(fd);
    return -1;
  }

  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    perror("connect");
    close(fd);
    return -1;
  }
  return fd;
}

int create_listener(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }

  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    perror("bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 1) != 0) {
    perror("listen");
    close(fd);
    return -1;
  }
  return fd;
}

bool read_raw_frame(int fd, uint32_t max_raw_frame_size, std::vector<uint8_t> *frame) {
  RawFrameHeader header;
  if (!read_exact(fd, &header, sizeof(header))) {
    return false;
  }

  const uint16_t key_value = ntohs(header.key_value);
  const uint32_t payload_size = ntohl(header.length);
  const uint32_t raw_frame_size = static_cast<uint32_t>(sizeof(RawFrameHeader)) + payload_size;
  if (key_value != RAW_FRAME_KEY_VALUE) {
    fprintf(stderr, "Invalid raw frame key: 0x%04x\n", key_value);
    return false;
  }
  if (raw_frame_size > max_raw_frame_size || raw_frame_size < sizeof(RawFrameHeader)) {
    fprintf(stderr, "Invalid raw frame size: %u\n", raw_frame_size);
    return false;
  }

  frame->resize(raw_frame_size);
  memcpy(frame->data(), &header, sizeof(header));
  if (payload_size > 0 && !read_exact(fd, frame->data() + sizeof(header), payload_size)) {
    return false;
  }
  return true;
}

void forward_downstream_to_upstream(int downstream_fd, int upstream_fd) {
  std::vector<uint8_t> buffer(64 * 1024);
  while (g_running.load()) {
    const ssize_t n = recv(downstream_fd, buffer.data(), buffer.size(), 0);
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (!write_exact(upstream_fd, buffer.data(), static_cast<size_t>(n))) {
      break;
    }
  }
  g_running.store(false);
}

}  // namespace

int main(int argc, char **argv) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  Options options;
  if (!parse_options(argc, argv, &options)) {
    usage(argv[0]);
    return 2;
  }

  printf("Connecting original TCP source %s:%u\n", options.source_host.c_str(), options.source_port);
  int upstream_fd = connect_tcp(options.source_host, options.source_port);
  if (upstream_fd < 0) {
    return 1;
  }

  printf("Listening stamped TCP port 0.0.0.0:%u\n", options.listen_port);
  int listener_fd = create_listener(options.listen_port);
  if (listener_fd < 0) {
    close(upstream_fd);
    return 1;
  }

  std::ofstream record_file;
  if (!options.record_path.empty()) {
    record_file.open(options.record_path.c_str(), std::ios::binary | std::ios::out | std::ios::app);
    if (!record_file.is_open()) {
      fprintf(stderr, "Failed to open record file: %s\n", options.record_path.c_str());
      close(listener_fd);
      close(upstream_fd);
      return 1;
    }
    printf("Recording stamped frames to %s\n", options.record_path.c_str());
  }

  printf("Waiting for one downstream client...\n");
  int downstream_fd = accept(listener_fd, nullptr, nullptr);
  if (downstream_fd < 0) {
    perror("accept");
    close(listener_fd);
    close(upstream_fd);
    return 1;
  }
  printf("Downstream client connected\n");

  std::thread control_thread(forward_downstream_to_upstream, downstream_fd, upstream_fd);

  uint64_t sequence_id = 0;
  uint64_t frame_count = 0;
  ForwardStats stats;
  stats.reset(clock_ns(CLOCK_MONOTONIC_RAW));
  std::vector<uint8_t> raw_frame;
  while (g_running.load()) {
    const uint64_t frame_start_ns = clock_ns(CLOCK_MONOTONIC_RAW);
    if (!read_raw_frame(upstream_fd, options.max_raw_frame_size, &raw_frame)) {
      fprintf(stderr, "Failed to read original raw frame\n");
      break;
    }
    const uint64_t read_done_ns = clock_ns(CLOCK_MONOTONIC_RAW);

    const RawFrameHeader *raw_header = reinterpret_cast<const RawFrameHeader *>(raw_frame.data());
    StampedFrameHeader stamped;
    memset(&stamped, 0, sizeof(stamped));
    stamped.magic = STAMPED_FRAME_MAGIC;
    stamped.version = STAMPED_FRAME_VERSION;
    stamped.header_size = sizeof(StampedFrameHeader);
    stamped.sequence_id = sequence_id++;
    stamped.capture_mono_ns = clock_ns(CLOCK_MONOTONIC_RAW);
    stamped.capture_wall_ns = clock_ns(CLOCK_REALTIME);
    stamped.raw_frame_size = static_cast<uint32_t>(raw_frame.size());
    stamped.raw_type = ntohs(raw_header->type);
    stamped.crc32 = crc32_ieee(raw_frame.data(), static_cast<uint32_t>(raw_frame.size()));
    const uint64_t stamp_crc_done_ns = clock_ns(CLOCK_MONOTONIC_RAW);

    const StampedFrameHeader network_stamped = to_network_order(stamped);
    if (!write_exact(downstream_fd, &network_stamped, sizeof(network_stamped)) ||
        !write_exact(downstream_fd, raw_frame.data(), raw_frame.size())) {
      fprintf(stderr, "Failed to write stamped frame to downstream client\n");
      break;
    }
    const uint64_t write_done_ns = clock_ns(CLOCK_MONOTONIC_RAW);

    if (record_file.is_open()) {
      record_file.write(reinterpret_cast<const char *>(&network_stamped), sizeof(network_stamped));
      record_file.write(reinterpret_cast<const char *>(raw_frame.data()), raw_frame.size());
      if (!record_file.good()) {
        fprintf(stderr, "Failed to record stamped frame\n");
        break;
      }
    }
    const uint64_t record_done_ns = clock_ns(CLOCK_MONOTONIC_RAW);

    ++frame_count;
    stats.add(read_done_ns - frame_start_ns,
              stamp_crc_done_ns - read_done_ns,
              write_done_ns - stamp_crc_done_ns,
              record_done_ns - write_done_ns,
              record_done_ns - read_done_ns,
              record_done_ns - frame_start_ns,
              sizeof(network_stamped) + raw_frame.size(),
              stamped.raw_type,
              stamped.raw_frame_size);
    if (frame_count % 100 == 0) {
      const uint64_t now_ns = clock_ns(CLOCK_MONOTONIC_RAW);
      stats.print(now_ns, frame_count);
      stats.reset(now_ns);
    }
  }

  g_running.store(false);
  shutdown(downstream_fd, SHUT_RDWR);
  shutdown(upstream_fd, SHUT_RDWR);
  close(downstream_fd);
  close(listener_fd);
  close(upstream_fd);
  if (control_thread.joinable()) {
    control_thread.join();
  }
  printf("Stopped\n");
  return 0;
}
