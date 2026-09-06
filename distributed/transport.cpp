#include "transport.hpp"

#include <cstring>
#include <stdexcept>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#endif

namespace recovery_planner::transport {

namespace {

std::uint32_t crc32c_bytes(const std::uint8_t* data, std::size_t len) noexcept {
  static std::uint32_t table[256] = {0};
  static const bool init = [] {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t crc = i;
      for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? (0x82F63B78u ^ (crc >> 1)) : (crc >> 1);
      table[i] = crc;
    }
    return true;
  }();
  (void)init;
  std::uint32_t crc = ~0u;
  for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}

std::uint32_t read_u32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}
void write_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
  p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  p[3] = static_cast<std::uint8_t>(v & 0xFFu);
}

#ifdef _WIN32
using sock_t = SOCKET;
constexpr sock_t kInvalidSocket = INVALID_SOCKET;
#else
using sock_t = int;
constexpr sock_t kInvalidSocket = -1;
#endif

std::int64_t recv_all(sock_t fd, std::uint8_t* buf, std::size_t len) {
  std::size_t got = 0;
  while (got < len) {
#ifdef _WIN32
    int n = recv(fd, reinterpret_cast<char*>(buf + got), static_cast<int>(len - got), 0);
#else
    std::int64_t n = recv(fd, buf + got, len - got, 0);
#endif
    if (n == 0) return -1;  // orderly shutdown
    if (n < 0) {
#ifdef _WIN32
      int err = WSAGetLastError();
      if (err == WSAEINTR || err == WSAEWOULDBLOCK) continue;
#else
      if (errno == EINTR) continue;
#endif
      return -1;
    }
    got += static_cast<std::size_t>(n);
  }
  return static_cast<std::int64_t>(got);
}

bool send_all(sock_t fd, const std::uint8_t* buf, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
#ifdef _WIN32
    int n = send(fd, reinterpret_cast<const char*>(buf + sent), static_cast<int>(len - sent), 0);
#else
    std::int64_t n = send(fd, buf + sent, len - sent, 0);
#endif
    if (n <= 0) {
#ifdef _WIN32
      int err = WSAGetLastError();
      if (err == WSAEINTR || err == WSAEWOULDBLOCK) continue;
#else
      if (errno == EINTR) continue;
#endif
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

void close_sock(sock_t fd) {
#ifdef _WIN32
  closesocket(fd);
#else
  ::close(fd);
#endif
}

}  // namespace

void transport_init() {
#ifdef _WIN32
  static bool inited = false;
  if (!inited) {
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
    inited = true;
  }
#else
  // no-op
#endif
}
void transport_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}

Listener::Listener() { transport_init(); }
Listener::~Listener() {
  if (handle_ != 0) {
    close_sock(static_cast<sock_t>(handle_));
    handle_ = 0;
  }
}

bool Listener::bind(std::uint16_t port) {
  sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kInvalidSocket) return false;
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close_sock(s);
    return false;
  }
  if (listen(s, 8) != 0) {
    close_sock(s);
    return false;
  }
  // Get the actual port.
  sockaddr_in bound{};
  socklen_t blen = sizeof(bound);
  getsockname(s, reinterpret_cast<sockaddr*>(&bound), &blen);
  port_ = ntohs(bound.sin_port);
  handle_ = static_cast<std::uint64_t>(s);
  return true;
}

std::shared_ptr<Connection> Listener::accept() {
  sockaddr_in cli{};
#ifdef _WIN32
  int clen = sizeof(cli);
  sock_t s = ::accept(static_cast<sock_t>(handle_), reinterpret_cast<sockaddr*>(&cli), &clen);
#else
  socklen_t clen = sizeof(cli);
  sock_t s = ::accept(static_cast<sock_t>(handle_), reinterpret_cast<sockaddr*>(&cli), &clen);
#endif
  if (s == kInvalidSocket) return nullptr;
  // Disable Nagle for our small messages; disable lingering.
  int opt = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));
  return std::make_shared<Connection>(static_cast<std::uint64_t>(s));
}

Connection::Connection(std::uint64_t socket) : socket_(socket) {}
Connection::~Connection() { close(); }

void Connection::close() {
  if (!closed_ && socket_ != 0) {
    close_sock(static_cast<sock_t>(socket_));
    socket_ = 0;
    closed_ = true;
  }
}

bool Connection::send_frame(const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaxFramePayload) return false;
  std::vector<std::uint8_t> header(9);
  write_u32(header.data(), kFrameMagic);
  header[4] = kFrameVersion;
  write_u32(header.data() + 5, static_cast<std::uint32_t>(payload.size()));
  std::uint32_t crc = crc32c_bytes(payload.data(), payload.size());
  std::vector<std::uint8_t> tail(4);
  write_u32(tail.data(), crc);

  bool ok = send_all(static_cast<sock_t>(socket_), header.data(), header.size());
  if (!ok) return false;
  if (!payload.empty()) ok = send_all(static_cast<sock_t>(socket_), payload.data(), payload.size());
  if (!ok) return false;
  return send_all(static_cast<sock_t>(socket_), tail.data(), tail.size());
}

bool Connection::recv_frame(std::vector<std::uint8_t>& payload) {
  std::uint8_t header[9];
  if (recv_all(static_cast<sock_t>(socket_), header, 9) < 0) return false;
  if (read_u32(header) != kFrameMagic) {
    throw std::runtime_error("malformed transport frame: bad magic");
  }
  if (header[4] != kFrameVersion) {
    throw std::runtime_error("malformed transport frame: unsupported version");
  }
  std::uint32_t len = read_u32(header + 5);
  if (len > kMaxFramePayload) {
    throw std::runtime_error("malformed transport frame: oversized payload");
  }
  payload.assign(len, 0);
  if (len > 0 && recv_all(static_cast<sock_t>(socket_), payload.data(), len) < 0) return false;
  std::uint8_t crcbuf[4];
  if (recv_all(static_cast<sock_t>(socket_), crcbuf, 4) < 0) return false;
  std::uint32_t expected = read_u32(crcbuf);
  if (crc32c_bytes(payload.data(), payload.size()) != expected) {
    throw std::runtime_error("malformed transport frame: checksum mismatch");
  }
  return true;
}

std::shared_ptr<Connection> connect_to(const Endpoint& ep) {
  transport_init();
  sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kInvalidSocket) return nullptr;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(ep.port);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close_sock(s);
    return nullptr;
  }
  return std::make_shared<Connection>(static_cast<std::uint64_t>(s));
}

}  // namespace recovery_planner::transport
