#pragma once

// Typed error model. Errors are categorized so callers and the CLI can react to
// semantics rather than string matching. Human-readable diagnostics accompany
// the typed status where appropriate.

#include "recovery_planner/types.hpp"

#include <stdexcept>
#include <string>

namespace recovery_planner {

// Stable, typed recovery error categories.
enum class ErrorCode : std::uint8_t {
  NONE = 0,
  INVALID_INPUT = 1,
  STALE_AUTHORITY = 2,
  STALE_EVIDENCE = 3,
  INSUFFICIENT_EVIDENCE = 4,
  INFEASIBLE_CANDIDATE = 5,
  COMPATIBILITY_REJECTION = 6,
  PERSISTENCE_CORRUPTION = 7,
  PROTOCOL_ERROR = 8,
  RESOURCE_EXHAUSTION = 9,
  EXECUTION_FAILURE = 10,
  AMBIGUOUS_OUTCOME = 11,
  CANCELLATION = 12,
  SHUTDOWN = 13,
  UNSUPPORTED = 14,
  NOT_FOUND = 15,
  ALREADY_EXISTS = 16,
  CONCURRENT_MODIFICATION = 17,
  INTERNAL = 18
};

inline const char* to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::NONE: return "none";
    case ErrorCode::INVALID_INPUT: return "invalid_input";
    case ErrorCode::STALE_AUTHORITY: return "stale_authority";
    case ErrorCode::STALE_EVIDENCE: return "stale_evidence";
    case ErrorCode::INSUFFICIENT_EVIDENCE: return "insufficient_evidence";
    case ErrorCode::INFEASIBLE_CANDIDATE: return "infeasible_candidate";
    case ErrorCode::COMPATIBILITY_REJECTION: return "compatibility_rejection";
    case ErrorCode::PERSISTENCE_CORRUPTION: return "persistence_corruption";
    case ErrorCode::PROTOCOL_ERROR: return "protocol_error";
    case ErrorCode::RESOURCE_EXHAUSTION: return "resource_exhaustion";
    case ErrorCode::EXECUTION_FAILURE: return "execution_failure";
    case ErrorCode::AMBIGUOUS_OUTCOME: return "ambiguous_outcome";
    case ErrorCode::CANCELLATION: return "cancellation";
    case ErrorCode::SHUTDOWN: return "shutdown";
    case ErrorCode::UNSUPPORTED: return "unsupported";
    case ErrorCode::NOT_FOUND: return "not_found";
    case ErrorCode::ALREADY_EXISTS: return "already_exists";
    case ErrorCode::CONCURRENT_MODIFICATION: return "concurrent_modification";
    case ErrorCode::INTERNAL: return "internal";
  }
  return "unknown";
}

// Strongly categorized exception carrying a typed status plus a diagnostic.
class RecoveryError : public std::runtime_error {
 public:
  RecoveryError(ErrorCode code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}

  ErrorCode code() const noexcept { return code_; }

 private:
  ErrorCode code_;
};

inline std::runtime_error make_error(ErrorCode code, std::string message) {
  return RecoveryError(code, std::move(message));
}

}  // namespace recovery_planner
