#ifndef STEPPE_TCP_TIMESTAMP_PROXY_STAMPED_FRAME_PROTOCOL_H_
#define STEPPE_TCP_TIMESTAMP_PROXY_STAMPED_FRAME_PROTOCOL_H_

#include <arpa/inet.h>
#include <stdint.h>

#define STAMPED_FRAME_MAGIC 0x53545046u
#define STAMPED_FRAME_VERSION 1u
#define RAW_FRAME_KEY_VALUE 0x1008u

#pragma pack(push, 1)
struct RawFrameHeader {
  uint16_t key_value;
  uint16_t type;
  uint32_t length;
};

struct StampedFrameHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint64_t sequence_id;
  uint64_t capture_mono_ns;
  uint64_t capture_wall_ns;
  uint32_t raw_frame_size;
  uint32_t raw_type;
  uint32_t crc32;
};
#pragma pack(pop)

inline uint64_t host_to_network_u64(uint64_t value) {
  const uint32_t high = htonl(static_cast<uint32_t>(value >> 32));
  const uint32_t low = htonl(static_cast<uint32_t>(value & 0xffffffffULL));
  return (static_cast<uint64_t>(low) << 32) | high;
}

inline uint64_t network_to_host_u64(uint64_t value) {
  const uint32_t high = ntohl(static_cast<uint32_t>(value >> 32));
  const uint32_t low = ntohl(static_cast<uint32_t>(value & 0xffffffffULL));
  return (static_cast<uint64_t>(low) << 32) | high;
}

inline StampedFrameHeader to_network_order(StampedFrameHeader header) {
  header.magic = htonl(header.magic);
  header.version = htons(header.version);
  header.header_size = htons(header.header_size);
  header.sequence_id = host_to_network_u64(header.sequence_id);
  header.capture_mono_ns = host_to_network_u64(header.capture_mono_ns);
  header.capture_wall_ns = host_to_network_u64(header.capture_wall_ns);
  header.raw_frame_size = htonl(header.raw_frame_size);
  header.raw_type = htonl(header.raw_type);
  header.crc32 = htonl(header.crc32);
  return header;
}

inline StampedFrameHeader from_network_order(StampedFrameHeader header) {
  header.magic = ntohl(header.magic);
  header.version = ntohs(header.version);
  header.header_size = ntohs(header.header_size);
  header.sequence_id = network_to_host_u64(header.sequence_id);
  header.capture_mono_ns = network_to_host_u64(header.capture_mono_ns);
  header.capture_wall_ns = network_to_host_u64(header.capture_wall_ns);
  header.raw_frame_size = ntohl(header.raw_frame_size);
  header.raw_type = ntohl(header.raw_type);
  header.crc32 = ntohl(header.crc32);
  return header;
}

inline uint32_t crc32_ieee(const uint8_t *data, uint32_t size) {
  uint32_t crc = 0xffffffffu;
  for (uint32_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

#endif
