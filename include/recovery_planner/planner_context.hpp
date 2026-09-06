#pragma once

// PlannerContext: the current, authoritative world view against which candidate
// evidence is compared. It carries the current generations and the set of
// currently-live worker / state / resource identities. The planner fenced
// every candidate against this context.

#include "recovery_planner/types.hpp"
#include "recovery_planner/evidence.hpp"

#include <map>
#include <set>
#include <string>
#include <cstdint>

namespace recovery_planner {

struct PlannerContext {
  // Current authority / generations.
  CoordinatorEpoch epoch;
  PlannerBootId planner_boot;
  WorkloadGeneration workload_generation;
  PolicyGeneration policy_generation;
  CompatibilityGeneration compatibility_generation;
  ResourceGeneration resource_generation;
  TopologyGeneration topology_generation;

  // Currently registered workers and their boot ids.
  std::map<WorkerId, WorkerBootId> worker_boots;
  std::set<WorkerId> alive_workers;

  // Available resources by kind.
  std::map<std::string, std::uint64_t> resource_available;

  // Reference compatibility table: subject identity -> set of compatible targets.
  std::map<std::string, std::set<std::string>> compatibility_table;

  // Recomputation authority: whether authoritative inputs and lineage exist for
  // a given workload generation.
  std::set<WorkloadGeneration> recompute_lineage_available;
  std::set<WorkloadGeneration> original_inputs_available;

  // ---- helpers -----------------------------------------------------------

  bool worker_is_current(WorkerId w) const noexcept {
    return alive_workers.count(w) != 0;
  }
  bool worker_boot_matches(WorkerId w, WorkerBootId boot) const noexcept {
    auto it = worker_boots.find(w);
    return it != worker_boots.end() && it->second == boot;
  }
  bool resource_sufficient(const std::string& kind, std::uint64_t required) const noexcept {
    auto it = resource_available.find(kind);
    if (it == resource_available.end()) return false;
    return it->second >= required;
  }

  // Reference compatibility check: is subject compatible with target?
  bool is_compatible(const std::string& subject,
                     const std::string& target) const noexcept {
    auto s = compatibility_table.find(subject);
    if (s == compatibility_table.end()) return false;
    return s->second.count(target) != 0;
  }

  // Whether recomputation is permitted / possible for a workload generation.
  bool recompute_available(WorkloadGeneration gen) const noexcept {
    return recompute_lineage_available.count(gen) != 0 &&
           original_inputs_available.count(gen) != 0;
  }

  // Record a worker registration.
  void register_worker(WorkerId w, WorkerBootId boot) {
    worker_boots[w] = boot;
    alive_workers.insert(w);
  }
  void unregister_worker(WorkerId w) {
    alive_workers.erase(w);
  }
};

inline const char* to_string(CompatibilityResult r) noexcept {
  switch (r) {
    case CompatibilityResult::EXACT: return "EXACT";
    case CompatibilityResult::COMPATIBLE: return "COMPATIBLE";
    case CompatibilityResult::COMPATIBLE_WITH_ADAPTATION: return "COMPATIBLE_WITH_ADAPTATION";
    case CompatibilityResult::CONDITIONAL: return "CONDITIONAL";
    case CompatibilityResult::INCOMPATIBLE: return "INCOMPATIBLE";
    case CompatibilityResult::UNKNOWN: return "UNKNOWN";
    case CompatibilityResult::INSUFFICIENT_EVIDENCE: return "INSUFFICIENT_EVIDENCE";
  }
  return "UNKNOWN";
}

}  // namespace recovery_planner
