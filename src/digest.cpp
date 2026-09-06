#include "recovery_planner/digest.hpp"

#include <cstring>

namespace recovery_planner {

namespace {
const std::uint32_t* crc32c_table() noexcept {
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
  return table;
}
}  // namespace

std::uint32_t crc32c(const std::uint8_t* data, std::size_t len, std::uint32_t seed) noexcept {
  const std::uint32_t* table = crc32c_table();
  std::uint32_t crc = ~seed;
  for (std::size_t i = 0; i < len; ++i) {
    crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return ~crc;
}

std::uint32_t crc32c(const std::vector<std::uint8_t>& data, std::uint32_t seed) noexcept {
  return crc32c(data.data(), data.size(), seed);
}

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t len) noexcept {
  std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  static const std::uint32_t k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

  std::vector<std::uint8_t> msg(data, data + len);
  std::uint64_t bit_len = static_cast<std::uint64_t>(len) * 8u;
  msg.push_back(0x80u);
  while (msg.size() % 64 != 56) msg.push_back(0x00u);
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<std::uint8_t>((bit_len >> (8 * i)) & 0xFFu));

  for (std::size_t off = 0; off < msg.size(); off += 64) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (static_cast<std::uint32_t>(msg[off + 4 * i]) << 24) |
             (static_cast<std::uint32_t>(msg[off + 4 * i + 1]) << 16) |
             (static_cast<std::uint32_t>(msg[off + 4 * i + 2]) << 8) |
             static_cast<std::uint32_t>(msg[off + 4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = (w[i - 15] >> 7) | (w[i - 15] << 25);
      s0 ^= (w[i - 15] >> 18) | (w[i - 15] << 14);
      s0 ^= w[i - 15] >> 3;
      std::uint32_t s1 = (w[i - 2] >> 17) | (w[i - 2] << 15);
      s1 ^= (w[i - 2] >> 19) | (w[i - 2] << 13);
      s1 ^= w[i - 2] >> 10;
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      std::uint32_t S1 = (e >> 6) | (e << 26);
      S1 ^= (e >> 11) | (e << 21);
      S1 ^= (e >> 25) | (e << 7);
      std::uint32_t ch = (e & f) ^ ((~e) & g);
      std::uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
      std::uint32_t S0 = (a >> 2) | (a << 30);
      S0 ^= (a >> 13) | (a << 19);
      S0 ^= (a >> 22) | (a << 10);
      std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      std::uint32_t temp2 = S0 + maj;
      hh = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  std::array<std::uint8_t, 32> out;
  for (int i = 0; i < 8; ++i) {
    out[4 * i] = static_cast<std::uint8_t>((h[i] >> 24) & 0xFFu);
    out[4 * i + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFFu);
    out[4 * i + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xFFu);
    out[4 * i + 3] = static_cast<std::uint8_t>(h[i] & 0xFFu);
  }
  return out;
}

std::string hex_encode(const std::uint8_t* data, std::size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    s.push_back(digits[(data[i] >> 4) & 0xF]);
    s.push_back(digits[data[i] & 0xF]);
  }
  return s;
}

std::string hex_encode(const std::vector<std::uint8_t>& data) {
  return hex_encode(data.data(), data.size());
}

}  // namespace recovery_planner
