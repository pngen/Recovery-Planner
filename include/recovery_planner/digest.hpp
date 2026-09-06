#pragma once

// Deterministic canonical encoding and integrity primitives.
//
// All durable/identity encodings use explicit big-endian byte order so that the
// same semantic object always produces the same canonical bytes across
// platforms. Integrity uses CRC-32C (Castagnoli) and, where a strong digest is
// wanted, SHA-256. These primitives are the foundation of identity digests and
// the versioned, integrity-checked persistence format.

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <vector>

namespace recovery_planner {

// ---------------------------------------------------------------------------
// CRC-32C (Castagnoli)
// ---------------------------------------------------------------------------
std::uint32_t crc32c(const std::uint8_t* data, std::size_t len, std::uint32_t seed = 0u) noexcept;
std::uint32_t crc32c(const std::vector<std::uint8_t>& data, std::uint32_t seed = 0u) noexcept;

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------
std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t len) noexcept;

// ---------------------------------------------------------------------------
// Canonical big-endian writer
// ---------------------------------------------------------------------------
class ByteWriter {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) {
    buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    buf_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
  }
  void u32(std::uint32_t v) {
    for (int i = 3; i >= 0; --i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
  }
  void u64(std::uint64_t v) {
    for (int i = 7; i >= 0; --i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
  }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void bytes(const std::uint8_t* data, std::size_t len) { buf_.insert(buf_.end(), data, data + len); }
  void cstring(const char* s) {
    while (s && *s) u8(static_cast<std::uint8_t>(*s++));
    u8(0);
  }
  std::size_t size() const noexcept { return buf_.size(); }
  const std::vector<std::uint8_t>& data() const noexcept { return buf_; }
  std::vector<std::uint8_t> take() { return std::move(buf_); }

 private:
  std::vector<std::uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Bounded canonical big-endian reader (untrusted data)
// ---------------------------------------------------------------------------
class ByteReader {
 public:
  explicit ByteReader(const std::uint8_t* data, std::size_t len) : data_(data), len_(len) {}
  explicit ByteReader(const std::vector<std::uint8_t>& v) {
    owned_ = v;
    data_ = owned_.data();
    len_ = owned_.size();
  }

  bool ok() const noexcept { return !failed_; }
  std::size_t remaining() const noexcept { return len_ - pos_; }
  bool has(std::size_t n) const noexcept { return !failed_ && remaining() >= n; }

  std::uint8_t u8() {
    if (!has(1)) { fail(); return 0; }
    return data_[pos_++];
  }
  std::uint16_t u16() {
    if (!has(2)) { fail(); return 0; }
    std::uint16_t v = static_cast<std::uint16_t>(data_[pos_] << 8 | data_[pos_ + 1]);
    pos_ += 2; return v;
  }
  std::uint32_t u32() {
    if (!has(4)) { fail(); return 0; }
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | data_[pos_ + i];
    pos_ += 4; return v;
  }
  std::uint64_t u64() {
    if (!has(8)) { fail(); return 0; }
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | data_[pos_ + i];
    pos_ += 8; return v;
  }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

  // Bounded read of a fixed number of bytes into an out-parameter vector.
  bool take(std::vector<std::uint8_t>& out, std::size_t n) {
    if (!has(n)) { fail(); return false; }
    out.assign(data_ + pos_, data_ + pos_ + n);
    pos_ += n;
    return true;
  }
  // Read a length-prefixed blob (u32 length), bounded by remaining bytes.
  bool take_blob(std::vector<std::uint8_t>& out) {
    if (!has(4)) { fail(); return false; }
    std::uint32_t n = u32();
    if (!has(n)) { fail(); return false; }
    return take(out, n);
  }
  // Read a NUL-terminated C string, bounded input.
  std::string cstring() {
    std::string s;
    while (true) {
      if (!has(1)) { fail(); return s; }
      char c = static_cast<char>(data_[pos_++]);
      if (c == '\0') break;
      s.push_back(c);
      if (s.size() > 4096) { fail(); break; }
    }
    return s;
  }

  std::size_t position() const noexcept { return pos_; }

 private:
  void fail() noexcept { failed_ = true; }
  const std::uint8_t* data_{nullptr};
  std::size_t len_{0};
  std::size_t pos_{0};
  bool failed_{false};
  std::vector<std::uint8_t> owned_;
};

// Hex-encode a byte buffer (lowercase, no separator).
std::string hex_encode(const std::uint8_t* data, std::size_t len);
std::string hex_encode(const std::vector<std::uint8_t>& data);

}  // namespace recovery_planner
