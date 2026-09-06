#include "recovery_planner/clock.hpp"

#include <chrono>

namespace recovery_planner {

Timestamp SystemClock::now() const noexcept {
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  return Timestamp(ns);
}

}  // namespace recovery_planner
