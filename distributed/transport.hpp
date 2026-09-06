#pragma once

// Framed TCP transport used by the Recovery Planner distributed proof.
//
// Real OS processes exchange real framed TCP over loopback. Frames are
// versioned, length-bounded, and CRC-32C protected, and are resilient to
// partial reads/writes and malformed/oversized frames. Portability: Winsock on
// Windows, POSIX sockets elsewhere.

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace recovery_planner::transport {

// Frame layout (all big-endian): magic(4) | version(1) | payload_len(4) | payload | crc32c(4)
constexpr std::uint32_t kFrameMagic = 0x52504631u;   // "RPF1"
constexpr std::uint8_t  kFrameVersion = 1u;
constexpr std::uint32_t kMaxFramePayload = 1024u * 1024u;  // 1 MiB bound

struct Endpoint {
  std::string host;
  std::uint16_t port{0};
};

// A TCP listener bound to loopback.
class Listener {
 public:
  Listener();
  ~Listener();
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  // Bind and start listening on the given port (0 = auto). Returns the actual port.
  bool bind(std::uint16_t port);
  // Accept one incoming connection (blocking).
  std::shared_ptr<class Connection> accept();
  std::uint16_t port() const { return port_; }

 private:
  std::uint64_t handle_{0};   // SOCKET
  std::uint16_t port_{0};
};

// A connected pipe.
class Connection {
 public:
  explicit Connection(std::uint64_t socket);
  ~Connection();
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // Send one frame. Blocking; handles partial writes. Returns false on failure.
  bool send_frame(const std::vector<std::uint8_t>& payload);
  // Receive one frame. Returns false on EOF/error; throws on malformed/oversized.
  bool recv_frame(std::vector<std::uint8_t>& payload);
  // Graceful shutdown.
  void close();

  std::uint64_t native() const { return socket_; }

 private:
  std::uint64_t socket_{0};
  bool closed_{false};
};

// Resolve and connect to a loopback endpoint, returning a connected connection.
std::shared_ptr<Connection> connect_to(const Endpoint& ep);

// Initialize the transport subsystem (idempotent). Call once per process.
void transport_init();
void transport_shutdown();

}  // namespace recovery_planner::transport
