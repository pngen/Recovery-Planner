#pragma once

// Recovery Planner - strongly typed identity and unit model.
//
// Identities and units are distinct C++ types so that accidental cross-domain
// mixing (e.g. a WorkerBootId where a CoordinatorEpoch was intended, or a
// ByteCount where a Duration was intended) fails at compile time rather than
// at run time. Equality, ordering, hashing and serialization are deterministic.

#include <cstdint>
#include <compare>
#include <functional>
#include <iosfwd>
#include <string>
#include <cstddef>

namespace recovery_planner {

// ---------------------------------------------------------------------------
// Identities
// ---------------------------------------------------------------------------

// A strong value identity. The Underlying representation is an unsigned integer
// unless otherwise noted; value 0 means "null / not assigned". Values are not
// interchangeable with one another or with raw integers.
template <typename Tag, typename Underlying = std::uint64_t>
class Id {
 public:
  using underlying_type = Underlying;
  using tag_type = Tag;

  constexpr Id() noexcept = default;
  constexpr explicit Id(Underlying value) noexcept : value_(value) {}
  constexpr Id(const Id&) noexcept = default;
  constexpr Id& operator=(const Id&) noexcept = default;
  constexpr ~Id() = default;

  constexpr Underlying value() const noexcept { return value_; }
  constexpr bool is_valid() const noexcept { return value_ != Underlying{0}; }
  constexpr bool is_null() const noexcept { return value_ == Underlying{0}; }
  constexpr explicit operator bool() const noexcept { return value_ != Underlying{0}; }

  // Produce the next identity value (used for generation advancement).
  constexpr Id next() const noexcept { return Id(static_cast<Underlying>(value_ + 1)); }
  constexpr Id& operator++() noexcept { ++value_; return *this; }
  constexpr Id operator++(int) noexcept { Id tmp = *this; ++value_; return tmp; }

  static constexpr Id null() noexcept { return Id(Underlying{0}); }

  friend constexpr bool operator==(Id a, Id b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(Id a, Id b) noexcept { return !(a == b); }
  friend constexpr bool operator<(Id a, Id b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator<=(Id a, Id b) noexcept { return a.value_ <= b.value_; }
  friend constexpr bool operator>(Id a, Id b) noexcept { return a.value_ > b.value_; }
  friend constexpr bool operator>=(Id a, Id b) noexcept { return a.value_ >= b.value_; }
  friend constexpr auto operator<=>(Id a, Id b) noexcept { return a.value_ <=> b.value_; }

  friend std::ostream& operator<<(std::ostream& os, Id id) {
    os << id.value();
    return os;
  }

 private:
  Underlying value_{0};
};

// Tag types. Each tag makes its corresponding Id a distinct type.
#define RP_ID_TAG(name) struct name##Tag {
#define RP_ID_TAG_END };
#define RP_ID_ALIAS(name) using name = Id<name##Tag>;
#define RP_ID_TAG_ONLY(name) struct name##Tag {};
#define RP_ID(name) struct name##Tag {}; using name = Id<name##Tag>;

// Define every identity used across the recovery-planning boundary.
RP_ID(CoordinatorEpoch)
RP_ID(PlannerBootId)
RP_ID(WorkloadId)
RP_ID(WorkloadGeneration)
RP_ID(ExecutionId)
RP_ID(ExecutionGeneration)
RP_ID(FailureId)
RP_ID(RecoveryRequestId)
RP_ID(RecoveryPlanId)
RP_ID(RecoveryPlanGeneration)
RP_ID(CandidateId)
RP_ID(CandidateGeneration)
RP_ID(WorkerId)
RP_ID(WorkerBootId)
RP_ID(EngineId)
RP_ID(EngineIncarnationId)
RP_ID(StateId)
RP_ID(StateGeneration)
RP_ID(CheckpointId)
RP_ID(CheckpointGeneration)
RP_ID(ResourceSnapshotId)
RP_ID(ResourceGeneration)
RP_ID(TopologyGeneration)
RP_ID(CompatibilityGeneration)
RP_ID(PolicyGeneration)
RP_ID(AttemptId)
RP_ID(DispatchId)

#undef RP_ID
#undef RP_ID_TAG
#undef RP_ID_TAG_END
#undef RP_ID_ALIAS
#undef RP_ID_TAG_ONLY

// ---------------------------------------------------------------------------
// Typed units
// ---------------------------------------------------------------------------

// A strongly typed quantity carried by a specific unit tag. Cross-unit mixing
// is a compile-time error. Arithmetic and comparison are deterministic.
template <typename Tag, typename Rep>
class Unit {
 public:
  using tag_type = Tag;
  using rep_type = Rep;

  constexpr Unit() noexcept = default;
  constexpr explicit Unit(Rep value) noexcept : value_(value) {}
  constexpr Unit(const Unit&) noexcept = default;
  constexpr Unit& operator=(const Unit&) noexcept = default;

  constexpr Rep value() const noexcept { return value_; }
  constexpr bool is_zero() const noexcept { return value_ == Rep{0}; }

  constexpr Unit operator+(Unit o) const noexcept { return Unit(static_cast<Rep>(value_ + o.value_)); }
  constexpr Unit operator-(Unit o) const noexcept { return Unit(static_cast<Rep>(value_ - o.value_)); }
  constexpr Unit operator-() const noexcept { return Unit(static_cast<Rep>(-value_)); }
  constexpr Unit& operator+=(Unit o) noexcept { value_ = static_cast<Rep>(value_ + o.value_); return *this; }
  constexpr Unit& operator-=(Unit o) noexcept { value_ = static_cast<Rep>(value_ - o.value_); return *this; }

  friend constexpr bool operator==(Unit a, Unit b) noexcept { return a.value_ == b.value_; }
  friend constexpr bool operator!=(Unit a, Unit b) noexcept { return !(a == b); }
  friend constexpr bool operator<(Unit a, Unit b) noexcept { return a.value_ < b.value_; }
  friend constexpr bool operator<=(Unit a, Unit b) noexcept { return a.value_ <= b.value_; }
  friend constexpr bool operator>(Unit a, Unit b) noexcept { return a.value_ > b.value_; }
  friend constexpr bool operator>=(Unit a, Unit b) noexcept { return a.value_ >= b.value_; }
  friend constexpr auto operator<=>(Unit a, Unit b) noexcept { return a.value_ <=> b.value_; }

  friend std::ostream& operator<<(std::ostream& os, Unit u) {
    os << u.value();
    return os;
  }

 private:
  Rep value_{0};
};

#define RP_UNIT(name, rep) struct name##Tag {}; using name = Unit<name##Tag, rep>;

RP_UNIT(Timestamp, std::int64_t)     // nanoseconds since an epoch (injectable clock)
RP_UNIT(Duration, std::int64_t)      // nanoseconds
RP_UNIT(ByteCount, std::uint64_t)    // bytes
RP_UNIT(ProgressUnits, std::uint64_t)// units of workload progress
RP_UNIT(CostUnits, std::uint64_t)    // abstract cost units
RP_UNIT(Bandwidth, std::uint64_t)    // bytes per second

#undef RP_UNIT

// ---------------------------------------------------------------------------
// Small value/status enums shared across the boundary
// ---------------------------------------------------------------------------

// Provenance of an evidence record: how the value was obtained.
enum class EvidenceProvenance : std::uint8_t {
  MEASURED = 0,   // observed directly on real hardware/runtime
  DERIVED = 1,    // computed from other trusted evidence
  ESTIMATED = 2,  // an explicit estimate, not observed
  REPORTED = 3,   // reported by an external party, not independently verified
  SYNTHETIC = 4,  // deterministic model/scenario used in place of physical topology
  UNKNOWN = 5
};

// Freshness of an evidence record relative to the current planning horizon.
enum class Freshness : std::uint8_t {
  CURRENT = 0,                 // valid now by contract
  STALE = 1,                   // valid but no longer current
  REVALIDATION_REQUIRED = 2,   // loaded from durable state; must be revalidated
  EXPIRED = 3,                 // past its validity horizon
  UNKNOWN = 4
};

// Evidence classification.
enum class EvidenceClassification : std::uint8_t {
  OBSERVED = 0,
  DERIVED = 1,
  ESTIMATED = 2,
  UNKNOWN = 3
};

// A fixed-point confidence in [0,1] scaled by 10000 (basis points).
class Confidence {
 public:
  constexpr Confidence() noexcept = default;
  constexpr explicit Confidence(std::uint16_t basis_points) noexcept : bp_(basis_points) {}

  // Build a confidence from an integer percentage (0..100).
  static constexpr Confidence from_percent(std::uint8_t percent) noexcept {
    return Confidence(static_cast<std::uint16_t>(static_cast<unsigned>(percent) * 100u));
  }
  static constexpr Confidence full() noexcept { return Confidence(10000); }
  static constexpr Confidence none() noexcept { return Confidence(0); }

  constexpr std::uint16_t basis_points() const noexcept { return bp_; }
  constexpr bool is_full() const noexcept { return bp_ == 10000; }
  constexpr bool is_zero() const noexcept { return bp_ == 0; }

  friend constexpr bool operator==(Confidence a, Confidence b) noexcept { return a.bp_ == b.bp_; }
  friend constexpr bool operator!=(Confidence a, Confidence b) noexcept { return !(a == b); }
  friend constexpr bool operator<(Confidence a, Confidence b) noexcept { return a.bp_ < b.bp_; }
  friend constexpr bool operator<=(Confidence a, Confidence b) noexcept { return a.bp_ <= b.bp_; }
  friend constexpr bool operator>(Confidence a, Confidence b) noexcept { return a.bp_ > b.bp_; }
  friend constexpr bool operator>=(Confidence a, Confidence b) noexcept { return a.bp_ >= b.bp_; }

 private:
  std::uint16_t bp_{0};
};

}  // namespace recovery_planner

namespace std {
// Deterministic hashing for identity and unit types.
template <typename Tag, typename Underlying>
struct hash<recovery_planner::Id<Tag, Underlying>> {
  size_t operator()(recovery_planner::Id<Tag, Underlying> id) const noexcept {
    return std::hash<Underlying>{}(id.value());
  }
};
template <typename Tag, typename Rep>
struct hash<recovery_planner::Unit<Tag, Rep>> {
  size_t operator()(recovery_planner::Unit<Tag, Rep> u) const noexcept {
    return std::hash<Rep>{}(u.value());
  }
};
}  // namespace std
