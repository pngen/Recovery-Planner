#pragma once

// Small, explicit message protocol for the distributed proof. The payload is a
// tagged message carried inside a framed transport frame. Every field is
// encoded big-endian and decoded with bounds checks.

#include <cstdint>
#include <string>
#include <vector>

namespace recovery_planner::proto {

enum class MsgType : std::uint8_t {
  NONE = 0,
  REGISTER = 1,        // worker -> coordinator: identity/authority publication
  REGISTER_ACK = 2,    // coordinator -> worker: accepted
  RUN = 3,             // coordinator -> worker: run work to a progress boundary
  WORK_DONE = 4,       // worker -> coordinator: produced durable recovery state
  DISPATCH = 5,        // coordinator -> worker: execute a recovery plan
  COMPLETE = 6,        // worker -> coordinator: recovery outcome (authoritative)
  PING = 7,
  STOP = 8
};

struct Msg {
  MsgType type{MsgType::NONE};
  std::uint64_t a{0};   // e.g. worker_id
  std::uint64_t b{0};   // e.g. boot_id / progress
  std::uint64_t c{0};   // e.g. generation / work_id
  std::uint64_t d{0};   // e.g. outcome code
  std::string s1;
  std::string s2;
  std::vector<std::uint8_t> blob;
};

// Encode a message into a payload (throwing on internal error). Bounded.
bool encode(const Msg& msg, std::vector<std::uint8_t>& out);

// Decode a payload with bounds checks. Returns false if malformed.
bool decode(const std::vector<std::uint8_t>& in, Msg& out);

inline const char* to_string(MsgType t) noexcept {
  switch (t) {
    case MsgType::REGISTER: return "REGISTER";
    case MsgType::REGISTER_ACK: return "REGISTER_ACK";
    case MsgType::RUN: return "RUN";
    case MsgType::WORK_DONE: return "WORK_DONE";
    case MsgType::DISPATCH: return "DISPATCH";
    case MsgType::COMPLETE: return "COMPLETE";
    case MsgType::PING: return "PING";
    case MsgType::STOP: return "STOP";
    default: return "NONE";
  }
}

}  // namespace recovery_planner::proto
