#pragma once

// Typed recovery evidence.
//
// Every decision must be supported by evidence. Evidence records carry their
// provenance (measured/derived/estimated/reported/synthetic/unknown), their
// freshness, a confidence, and the observing identity. Absent evidence is
// represented by std::optional being empty and must never be converted into
// optimistic validity.

#include "recovery_planner/types.hpp"
#include "recovery_planner/clock.hpp"

#include <optional>
#include <string>
#include <vector>

namespace recovery_planner {

// ---------------------------------------------------------------------------
// Small status enums
// ---------------------------------------------------------------------------
enum class StateIntegrity : std::uint8_t {
  VALID = 0,
  CORRUPT = 1,
  UNCERTAIN = 2,
  NOT_VERIFIED = 3,
  UNKNOWN = 4
};

enum class CompatibilityResult : std::uint8_t {
  EXACT = 0,
  COMPATIBLE = 1,
  COMPATIBLE_WITH_ADAPTATION = 2,
  CONDITIONAL = 3,
  INCOMPATIBLE = 4,
  UNKNOWN = 5,
  INSUFFICIENT_EVIDENCE = 6
};

enum class CandidateReadiness : std::uint8_t {
  READY = 0,
  NOT_READY = 1,
  WARMING = 2,
  UNKNOWN = 3
};

enum class ResourceStatus : std::uint8_t {
  AVAILABLE = 0,
  UNAVAILABLE = 1,
  CONTENDED = 2,
  UNKNOWN = 3
};

enum class StorageLocation : std::uint8_t {
  LOCAL = 0,
  REMOTE = 1,
  REPLICATED = 2,
  WARM_MEMORY = 3,
  SHADOW = 4,
  RECOMPUTABLE = 5,
  UNKNOWN = 6
};

// ---------------------------------------------------------------------------
// Evidence metadata (shared by every evidence record)
// ---------------------------------------------------------------------------
struct EvidenceMeta {
  CoordinatorEpoch coordinator_epoch;   // epoch under which this was observed
  WorkerBootId observer;                // identity of the observer
  Timestamp observed_at;                // when it was observed
  EvidenceProvenance provenance{EvidenceProvenance::UNKNOWN};
  Freshness freshness{Freshness::UNKNOWN};
  Confidence confidence;                // none by default
  bool persisted{false};                // true if loaded from durable storage
};

// Conservative post-restart policy: dynamic observed evidence loaded from
// durable state must not silently become CURRENT. It becomes
// REVALIDATION_REQUIRED unless it is intrinsically durable by contract.
inline void mark_revalidation_required(EvidenceMeta& meta) {
  if (meta.freshness != Freshness::CURRENT) {
    // preserve an explicit STALE/EXPIRED marker, otherwise require revalidation
    meta.freshness = Freshness::REVALIDATION_REQUIRED;
  }
}

// ---------------------------------------------------------------------------
// Concrete evidence records
// ---------------------------------------------------------------------------
struct StateEvidence {
  EvidenceMeta meta;
  StateId state_id;
  StateGeneration state_generation;
  WorkloadGeneration workload_generation;
  CheckpointGeneration checkpoint_generation;
  StateIntegrity integrity{StateIntegrity::UNKNOWN};
  ByteCount size_bytes;
  ByteCount restore_bytes;         // bytes that must be read/restored
  ProgressUnits preserved_progress; // authoritative progress preserved by this state
  ProgressUnits durable_progress;   // progress that is durably committed
  StorageLocation location{StorageLocation::UNKNOWN};
  bool is_durable{false};
  std::string identity;            // workload/runtime/state format identity
};

struct CompatibilityEvidence {
  EvidenceMeta meta;
  CompatibilityGeneration compatibility_generation;
  CompatibilityResult result{CompatibilityResult::UNKNOWN};
  std::string subject_identity;    // workload/runtime identity
  std::string state_format;
  std::string reason;
};

struct AvailabilityEvidence {
  EvidenceMeta meta;
  WorkerId worker;
  WorkerBootId boot;
  EngineIncarnationId engine_incarnation;
  bool is_alive{false};
  CandidateReadiness readiness{CandidateReadiness::UNKNOWN};
};

struct ResourceEvidence {
  EvidenceMeta meta;
  ResourceSnapshotId snapshot;
  ResourceGeneration generation;
  std::string resource_kind;
  // required amount available; std::nullopt means unknown
  std::optional<std::uint64_t> required;
  std::optional<std::uint64_t> available;
  ResourceStatus status{ResourceStatus::UNKNOWN};
};

struct TopologyEvidence {
  EvidenceMeta meta;
  TopologyGeneration generation;
  bool path_possible{false};
  std::string source;      // source location/name
  std::string destination; // destination location/name
  // optional measured path latency/cost
  std::optional<Duration> path_latency;
};

struct TransferEvidence {
  EvidenceMeta meta;
  ByteCount transfer_bytes;
  std::optional<Bandwidth> bandwidth;
  std::optional<Duration> transfer_duration; // estimated duration if known
  std::string from;
  std::string to;
};

struct CheckpointEvidence {
  EvidenceMeta meta;
  CheckpointId checkpoint_id;
  CheckpointGeneration checkpoint_generation;
  StateGeneration state_generation;
  Duration age;                    // how old the checkpoint is now
  StateIntegrity integrity{StateIntegrity::UNKNOWN};
  bool is_durable{false};
};

struct ShadowEvidence {
  EvidenceMeta meta;
  WorkerId shadow_worker;
  WorkerBootId shadow_boot;
  StateGeneration shadow_state_generation;
  Duration lag;                        // how far behind the primary it is
  Timestamp last_sync_point;
  CandidateReadiness readiness{CandidateReadiness::UNKNOWN};
  CompatibilityResult compatibility{CompatibilityResult::UNKNOWN};
};

struct ProgressEvidence {
  EvidenceMeta meta;
  ProgressUnits committed;       // committed by the workload
  ProgressUnits durable;         // durably persisted
  ProgressUnits recoverable;     // recoverable from declared state sources
  ProgressUnits recomputable;    // recomputable from authoritative inputs
  ProgressUnits lost;            // definitively lost
  // unknown = total - (committed|durable|recoverable|recomputable|lost) where total known
  std::optional<ProgressUnits> total;
};

struct DeadlineEvidence {
  EvidenceMeta meta;
  std::optional<Timestamp> recovery_deadline;   // absolute deadline
  std::optional<Duration> max_recovery_duration; // relative allowance
  std::optional<ProgressUnits> max_tolerated_progress_loss;
};

struct CostEvidence {
  EvidenceMeta meta;
  std::optional<Duration> expected_time;     // total expected recovery latency
  std::optional<Duration> restore_time;
  std::optional<Duration> restart_time;
  std::optional<Duration> transfer_time;
  std::optional<Duration> recompute_time;
  std::optional<ByteCount> bytes_transferred;
  std::optional<ByteCount> bytes_read;
  std::optional<ByteCount> bytes_materialized;
  std::optional<CostUnits> cost_units;
  std::optional<CostUnits> energy_cost;
  std::optional<ProgressUnits> expected_lost_progress;
  std::optional<ProgressUnits> expected_preserved_progress;
  std::optional<Duration> checkpoint_age;
  std::optional<Duration> deadline_slack;
  std::uint8_t failover_domain_risk{0};
};

struct AuthorityEvidence {
  EvidenceMeta meta;
  CoordinatorEpoch coordinator_epoch;
  PolicyGeneration policy_generation;
  bool coordinator_authority_valid{false};
  bool policy_authority_valid{false};
  bool candidate_authority_valid{false};
  bool execution_authority_valid{false};
};

}  // namespace recovery_planner
