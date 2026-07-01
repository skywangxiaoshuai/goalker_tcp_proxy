# TCP Timestamp Proxy

This is a standalone Linux C++ tool. It does not depend on ROS.

It connects to the original robot TCP stream, reads complete raw frames in the
existing `[MSG_HEADER][payload]` format, timestamps each frame on the robot
host, then forwards a stamped stream on a new TCP port.

## Build

```bash
cd tools/steppe_sim/tcp_timestamp_proxy
chmod +x build.sh
./build.sh
```

Cross-build for the aarch64 robot from Ubuntu:

```bash
sudo apt update
sudo apt install -y g++-aarch64-linux-gnu

cd tools/steppe_sim/tcp_timestamp_proxy
chmod +x build_aarch64.sh
./build_aarch64.sh
```

The aarch64 binaries are written to:

```text
build/aarch64/tcp_timestamp_proxy
build/aarch64/stamped_frame_dump
```

## Run on the robot

Default original stream:

```text
127.0.0.1:9002
```

Default stamped output:

```text
0.0.0.0:9102
```

Run without local recording:

```bash
chmod +x run_proxy.sh
./run_proxy.sh
```

The script defaults to:

```text
source: 127.0.0.1:9002
listen: 0.0.0.0:9102
record: disabled
```

You can override ports with environment variables:

```bash
SOURCE_HOST=127.0.0.1 SOURCE_PORT=9002 LISTEN_PORT=9102 ./run_proxy.sh
```

Or run the binary directly:

```bash
./build/tcp_timestamp_proxy \
  --source-host 127.0.0.1 \
  --source-port 9002 \
  --listen-port 9102
```

With local stamped raw-frame recording:

```bash
./build/tcp_timestamp_proxy \
  --source-host 127.0.0.1 \
  --source-port 9002 \
  --listen-port 9102 \
  --record-path /tmp/steppe_stamped_frames.bin
```

## Verify from another machine

```bash
./build/stamped_frame_dump \
  --host <robot-ip> \
  --port 9102 \
  --max-frames 20
```

Expected output:

```text
seq=0 type=0x00a1 size=... mono_ns=... wall_ns=... crc=ok
seq=1 type=0x00a2 size=... mono_ns=... wall_ns=... crc=ok
```

## Wire Format

Forwarded data:

```text
[StampedFrameHeader][raw MSG_HEADER][raw payload]
```

`StampedFrameHeader` fields are stored in network byte order.

```cpp
struct StampedFrameHeader {
  uint32_t magic;            // 0x53545046, "STPF"
  uint16_t version;          // 1
  uint16_t header_size;      // sizeof(StampedFrameHeader)
  uint64_t sequence_id;
  uint64_t capture_mono_ns;  // CLOCK_MONOTONIC_RAW
  uint64_t capture_wall_ns;  // CLOCK_REALTIME
  uint32_t raw_frame_size;   // sizeof(MSG_HEADER) + payload length
  uint32_t raw_type;         // original MSG_HEADER.Type
  uint32_t crc32;            // crc32 of raw frame
};
```

The timestamp is taken immediately after a complete raw TCP frame has been read
by the proxy.

## Notes

- Downstream client data is forwarded back to the original TCP connection
  unchanged. This keeps existing control packets usable.
- The current RViz monitor does not parse the stamped protocol yet. Use
  `stamped_frame_dump` first to verify the robot-side timestamp proxy.
