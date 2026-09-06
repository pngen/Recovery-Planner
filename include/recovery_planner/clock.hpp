#pragma once

// Injectable clock abstraction. Deadlines, freshness, recovery age and plan
// expiration all derive from the injectable clock so that deterministic time
// tests do not depend on real-time sleeps.

#include "recovery_planner/types.hpp"

#include <cstdint>

namespace recovery_planner {

// Abstract clock. All timestamps are nanoseconds since an arbitrary epoch.
class Clock {
 public:
  virtual ~Clock() = default;

  // Current wall-clock time.
  virtual Timestamp now() const noexcept = 0;

  // Human-readable label (for diagnostics).
  virtual const char* name() const noexcept = 0;
};

// Real system clock. Suitable for integration tests and the distributed proof.
class SystemClock final : public Clock {
 public:
  Timestamp now() const noexcept override;
  const char* name() const noexcept override { return "system"; }
};

// A deterministic, manually-advanced test clock. Tests advance it explicitly;
// no sleeps are used as the primary mechanism for deterministic time state.
class TestClock final : public Clock {
 public:
  explicit TestClock(Timestamp start = Timestamp(0)) : current_(start) {}
  void advance(Duration d) noexcept { current_ = Timestamp(current_.value() + d.value()); }
  void set(Timestamp t) noexcept { current_ = t; }
  Timestamp now() const noexcept override { return current_; }
  const char* name() const noexcept override { return "test"; }

 private:
  Timestamp current_;
};

}  // namespace recovery_planner
