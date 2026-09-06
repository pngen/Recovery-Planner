#include "protocol.hpp"

#include <cstring>

namespace recovery_planner::proto {

namespace {
void put_u64(std::vector<std::uint8_t>& v, std::uint64_t x) {
  for (int i = 7; i >= 0; --i) v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFFu));
}
std::uint64_t get_u64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}
void put_u8(std::vector<std::uint8_t>& v, std::uint8_t x) { v.push_back(x); }
void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  for (int i = 3; i >= 0; --i) v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFFu));
}
std::uint32_t get_u32(const std::uint8_t* p) {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v = (v << 8) | p[i];
  return v;
}
void put_str(std::vector<std::uint8_t>& v, const std::string& s) {
  put_u32(v, static_cast<std::uint32_t>(s.size()));
  for (char ch : s) v.push_back(static_cast<std::uint8_t>(ch));
}
void put_blob(std::vector<std::uint8_t>& v, const std::vector<std::uint8_t>& b) {
  put_u32(v, static_cast<std::uint32_t>(b.size()));
  v.insert(v.end(), b.begin(), b.end());
}

constexpr std::uint32_t kStrMax = 65536;
constexpr std::uint32_t kBlobMax = 1024 * 1024;

bool get_str(const std::uint8_t* p, std::size_t& pos, std::size_t len, std::string& s) {
  if (pos + 4 > len) return false;
  std::uint32_t n = get_u32(p + pos);
  pos += 4;
  if (n > kStrMax) return false;
  if (pos + n > len) return false;
  s.assign(reinterpret_cast<const char*>(p + pos), n);
  pos += n;
  return true;
}
bool get_blob(const std::uint8_t* p, std::size_t& pos, std::size_t len, std::vector<std::uint8_t>& b) {
  if (pos + 4 > len) return false;
  std::uint32_t n = get_u32(p + pos);
  pos += 4;
  if (n > kBlobMax) return false;
  if (pos + n > len) return false;
  b.assign(p + pos, p + pos + n);
  pos += n;
  return true;
}
}  // namespace

bool encode(const Msg& msg, std::vector<std::uint8_t>& out) {
  out.clear();
  put_u8(out, static_cast<std::uint8_t>(msg.type));
  put_u64(out, msg.a);
  put_u64(out, msg.b);
  put_u64(out, msg.c);
  put_u64(out, msg.d);
  put_str(out, msg.s1);
  put_str(out, msg.s2);
  put_blob(out, msg.blob);
  return true;
}

bool decode(const std::vector<std::uint8_t>& in, Msg& out) {
  std::size_t pos = 0;
  if (in.size() < 1 + 32) return false;
  out.type = static_cast<MsgType>(in[pos++]);
  out.a = get_u64(in.data() + pos); pos += 8;
  out.b = get_u64(in.data() + pos); pos += 8;
  out.c = get_u64(in.data() + pos); pos += 8;
  out.d = get_u64(in.data() + pos); pos += 8;
  if (!get_str(in.data(), pos, in.size(), out.s1)) return false;
  if (!get_str(in.data(), pos, in.size(), out.s2)) return false;
  if (!get_blob(in.data(), pos, in.size(), out.blob)) return false;
  return pos == in.size();
}

}  // namespace recovery_planner::proto
