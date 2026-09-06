#pragma once

// Recovery strategy semantics. These are planning-level semantics only; the
// planner never fakes execution of mechanisms owned by adjacent runtimes.

#include "recovery_planner/types.hpp"

#include <cstdint>
#include <string>

namespace recovery_planner {

enum class RecoveryStrategy : std::uint8_t {
  RESTORE = 0,
  RESTART = 1,
  MIGRATE = 2,
  REHYDRATE = 3,
  SHADOW_PROMOTE = 4,
  FAILOVER = 5,
  RECOMPUTE = 6
};

inline const char* to_string(RecoveryStrategy s) noexcept {
  switch (s) {
    case RecoveryStrategy::RESTORE: return "RESTORE";
    case RecoveryStrategy::RESTART: return "RESTART";
    case RecoveryStrategy::MIGRATE: return "MIGRATE";
    case RecoveryStrategy::REHYDRATE: return "REHYDRATE";
    case RecoveryStrategy::SHADOW_PROMOTE: return "SHADOW_PROMOTE";
    case RecoveryStrategy::FAILOVER: return "FAILOVER";
    case RecoveryStrategy::RECOMPUTE: return "RECOMPUTE";
  }
  return "UNKNOWN";
}

}  // namespace recovery_planner
