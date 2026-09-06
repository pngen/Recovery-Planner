#pragma once

// RecoveryCandidate: an explicit proposal of a recovery path.
//
// A candidate merely exists; its existence never implies feasibility. The
// deterministic feasibility evaluator explicitly returns FEASIBLE, INFEASIBLE,
// DEFER, REVALIDATION_REQUIRED, or INSUFFICIENT_EVIDENCE.

#include "recovery_planner/types.hpp"
#include "recovery_planner/evidence.hpp"
#include "recovery_planner/strategy.hpp"

#include <optional>
#include <string>
#include <vector>

namespace recovery_planner {

struct RecoveryCandidate {
  CandidateId candidate_id;
  CandidateGeneration generation;
  RecoveryStrategy strategy{RecoveryStrategy::RESTART};

  // Target / source identities.
  WorkerId target_worker;
  WorkerBootId target_boot;
  EngineId target_engine;
  EngineIncarnationId target_engine_incarnation;
  WorkerId source_worker;             // for migrate, the original worker
  WorkerBootId source_boot;

  // State source.
  StateId state_source;
  StateGeneration state_generation;
  CheckpointId checkpoint_source;
  CheckpointGeneration checkpoint_generation;

  // Evidence bundles (empty optional == absent evidence == unknown).
  std::optional<StateEvidence> state;
  std::optional<CompatibilityEvidence> compatibility;
  std::optional<AvailabilityEvidence> availability;
  std::optional<ResourceEvidence> resource;
  std::optional<TopologyEvidence> topology;
  std::optional<TransferEvidence> transfer;
  std::optional<CheckpointEvidence> checkpoint;
  std::optional<ShadowEvidence> shadow;
  std::optional<ProgressEvidence> progress;
  std::optional<CostEvidence> cost;
  std::optional<AuthorityEvidence> authority;

  // Why this candidate exists (narrative, for explanation).
  std::string reason_for_existence;

  // Candidate capabilities / required capabilities (opaque to core).
  std::vector<std::string> capabilities;
  std::vector<std::string> required_capabilities;

  bool has_strategy(RecoveryStrategy s) const noexcept { return strategy == s; }
};

}  // namespace recovery_planner
