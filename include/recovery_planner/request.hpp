#pragma once

// RecoveryRequest: the problem to solve.
//
// Absence of a field is represented explicitly (std::optional empty / empty
// vector) and is never guessed. The planner does not fabricate values for
// missing objectives.

#include "recovery_planner/types.hpp"
#include "recovery_planner/clock.hpp"
#include "recovery_planner/evidence.hpp"
#include "recovery_planner/strategy.hpp"

#include <optional>
#include <string>
#include <vector>

namespace recovery_planner {

struct RecoveryRequest {
  RecoveryRequestId request_id;
  WorkloadId workload;
  WorkloadGeneration workload_generation;
  ExecutionId execution;
  ExecutionGeneration execution_generation;
  FailureId failure;
  std::string reason;                  // human/typed description of failure
  ProgressEvidence prior_progress;

  // Deadlines / objectives (absent if not required).
  std::optional<Timestamp> recovery_deadline;
  std::optional<Duration> max_recovery_duration;
  std::optional<ProgressUnits> max_tolerated_progress_loss;

  // Preferred / forbidden strategies.
  std::vector<RecoveryStrategy> preferred_strategies;
  std::vector<RecoveryStrategy> forbidden_strategies;

  // Requirements and constraints (opaque to the core, interpreted by adapters).
  std::vector<std::string> state_requirements;
  std::vector<std::string> placement_constraints;
  std::vector<std::string> compatibility_requirements;

  // Authority / generation context.
  CoordinatorEpoch coordinator_epoch;
  PolicyGeneration policy_generation;
  TopologyGeneration topology_generation;

  // Injectablable-clock timestamp of the request.
  Timestamp requested_at;

  // Optional value/cost priorities; empty means no priority weighting is
  // supplied and the default deterministic ordering applies.
  std::vector<std::pair<std::string, std::uint64_t>> value_priorities;
};

}  // namespace recovery_planner
