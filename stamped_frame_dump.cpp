#include "stamped_frame_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

struct Options {
  std::string host = "127.0.0.1";
  uint16_t port = 9102;
  uint64_t max_frames = 0;
};

void usage(const char *program) {
  printf(
      "Usage: %s [options]\n"
      "Options:\n"
      "  --host <host>       Stamped proxy host, default 127.0.0.1\n"
      "  --port <port>       Stamped proxy port, default 9102\n"
      "  --max-frames <n>    Stop after n frames, default unlimited\n"
      "  --help              Show this help\n",
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

bool parse_u64(const char *text, uint64_t *value) {
  char *end = nullptr;
  const unsigned long long parsed = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *value = static_cast<uint64_t>(parsed);
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

    if (arg == "--host") {
      const char *value = need_value("--host");
      if (!value) return false;
      options->host = value;
    } else if (arg == "--port") {
      const char *value = need_value("--port");
      if (!value || !parse_u16(value, &options->port)) return false;
    } else if (arg == "--max-frames") {
      const char *value = need_value("--max-frames");
      if (!value || !parse_u64(value, &options->max_frames)) return false;
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

bool read_exact(int fd, void *buffer, size_t size) {
  uint8_t *cursor = static_cast<uint8_t *>(buffer);
  size_t remaining = size;
  while (remaining > 0) {
    const ssize_t n = recv(fd, cursor, remaining, 0);
    if (n == 0) return false;
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    cursor += n;
    remaining -= static_cast<size_t>(n);
  }
  return true;
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
    fprintf(stderr, "Invalid host: %s\n", host.c_str());
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

}  // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse_options(argc, argv, &options)) {
    usage(argv[0]);
    return 2;
  }

  int fd = connect_tcp(options.host, options.port);
  if (fd < 0) {
    return 1;
  }

  uint64_t count = 0;
  while (options.max_frames == 0 || count < options.max_frames) {
    StampedFrameHeader network_header;
    if (!read_exact(fd, &network_header, sizeof(network_header))) {
      fprintf(stderr, "Failed to read stamped header\n");
      break;
    }

    StampedFrameHeader header = from_network_order(network_header);
    if (header.magic != STAMPED_FRAME_MAGIC ||
        header.version != STAMPED_FRAME_VERSION ||
        header.header_size != sizeof(StampedFrameHeader)) {
      fprintf(stderr, "Invalid stamped header magic/version/size\n");
      break;
    }
    if (header.raw_frame_size < sizeof(RawFrameHeader) ||
        header.raw_frame_size > 20u * 1024u * 1024u) {
      fprintf(stderr, "Invalid raw frame size: %u\n", header.raw_frame_size);
      break;
    }

    std::vector<uint8_t> raw_frame(header.raw_frame_size);
    if (!read_exact(fd, raw_frame.data(), raw_frame.size())) {
      fprintf(stderr, "Failed to read raw frame\n");
      break;
    }
    const uint32_t crc = crc32_ieee(raw_frame.data(), static_cast<uint32_t>(raw_frame.size()));
    const bool crc_ok = crc == header.crc32;

    printf(
        "seq=%llu type=0x%04x size=%u mono_ns=%llu wall_ns=%llu crc=%s\n",
        static_cast<unsigned long long>(header.sequence_id),
        static_cast<unsigned int>(header.raw_type),
        static_cast<unsigned int>(header.raw_frame_size),
        static_cast<unsigned long long>(header.capture_mono_ns),
        static_cast<unsigned long long>(header.capture_wall_ns),
        crc_ok ? "ok" : "bad");
    fflush(stdout);
    ++count;
  }

  close(fd);
  return 0;
}
