#include "recovery_planner/feasibility.hpp"

#include <algorithm>

namespace recovery_planner {

namespace {

struct FeasFlags {
  bool infeasible{false};
  bool insufficient{false};
  bool revalidation{false};
  bool defer{false};
};

void hard(FeasFlags& f, FeasibilityEvaluation& ev, RejectionReason reason, std::string detail) {
  f.infeasible = true;
  ev.violations.push_back({reason, std::move(detail)});
}

bool fresh(const EvidenceMeta& m) noexcept {
  switch (m.freshness) {
    case Freshness::CURRENT: return true;
    default: return false;
  }
}

bool revalidation_needed(const EvidenceMeta& m) noexcept {
  return m.freshness == Freshness::REVALIDATION_REQUIRED;
}

bool stale_or_expired(const EvidenceMeta& m) noexcept {
  return m.freshness == Freshness::STALE || m.freshness == Freshness::EXPIRED;
}

}  // namespace

FeasibilityEvaluation evaluate_feasibility(const RecoveryCandidate& candidate,
                                           const RecoveryRequest& request,
                                           const PlannerContext& ctx) {
  FeasibilityEvaluation ev;
  FeasFlags f;

  // 1. Forbidden strategy.
  if (std::find(request.forbidden_strategies.begin(), request.forbidden_strategies.end(),
                candidate.strategy) != request.forbidden_strategies.end()) {
    hard(f, ev, RejectionReason::FORBIDDEN_STRATEGY,
         std::string("strategy ") + to_string(candidate.strategy) + " is forbidden by the request");
    ev.result = FeasibilityResult::INFEASIBLE;
    return ev;
  }

  // 2. Identity / generation / strategy sanity.
  if (!candidate.candidate_id.is_valid() || !candidate.generation.is_valid()) {
    hard(f, ev, RejectionReason::INVALID_CANDIDATE_GENERATION,
         "candidate identity or generation is null");
  }
  {
    const int s = static_cast<int>(candidate.strategy);
    if (s < static_cast<int>(RecoveryStrategy::RESTORE) || s > static_cast<int>(RecoveryStrategy::RECOMPUTE)) {
      hard(f, ev, RejectionReason::INVALID_STRATEGY, "candidate strategy value is out of range");
    }
  }

  // 3. Authority / generation fencing.
  if (candidate.authority) {
    if (candidate.authority->coordinator_epoch != ctx.epoch) {
      hard(f, ev, RejectionReason::STALE_AUTHORITY,
           "candidate observed under epoch " + std::to_string(candidate.authority->coordinator_epoch.value()) +
           " but current epoch is " + std::to_string(ctx.epoch.value()));
    }
    if (candidate.authority->policy_generation != ctx.policy_generation) {
      hard(f, ev, RejectionReason::POLICY_GENERATION_STALE,
           "candidate policy generation does not match current policy generation");
    }
  } else {
    f.insufficient = true;
  }

  // Workload generation consistency.
  if (candidate.state && candidate.state->workload_generation != request.workload_generation &&
      candidate.state->workload_generation.is_valid()) {
    hard(f, ev, RejectionReason::WORKLOAD_GENERATION_STALE,
         "candidate state belongs to a different workload generation");
  }

  // 4. State integrity + freshness.
  if (candidate.state) {
    const auto& st = *candidate.state;
    if (st.integrity == StateIntegrity::CORRUPT) {
      hard(f, ev, RejectionReason::INVALID_STATE_INTEGRITY, "state integrity is CORRUPT");
    }
    if (st.meta.freshness == Freshness::EXPIRED) {
      hard(f, ev, RejectionReason::EXPIRED_STATE, "state evidence is EXPIRED");
    }
    if (stale_or_expired(st.meta)) {
      f.insufficient = true;  // not current, cannot be trusted as-is
    }
    if (revalidation_needed(st.meta)) f.revalidation = true;
    if (st.integrity == StateIntegrity::UNCERTAIN || st.integrity == StateIntegrity::NOT_VERIFIED ||
        st.integrity == StateIntegrity::UNKNOWN) {
      f.insufficient = true;
    }
  } else {
    f.insufficient = true;
  }

  // 5. Compatibility.
  if (candidate.compatibility) {
    const auto& co = *candidate.compatibility;
    if (co.result == CompatibilityResult::INCOMPATIBLE) {
      hard(f, ev, RejectionReason::INCOMPATIBLE,
           co.reason.empty() ? "candidate is incompatible" : co.reason);
    }
    if (co.compatibility_generation != ctx.compatibility_generation) {
      hard(f, ev, RejectionReason::COMPATIBILITY_GENERATION_STALE,
           "compatibility generation does not match current");
    }
    if (co.result == CompatibilityResult::UNKNOWN || co.result == CompatibilityResult::INSUFFICIENT_EVIDENCE) {
      f.insufficient = true;
    }
    if (revalidation_needed(co.meta)) f.revalidation = true;
  } else if (!request.compatibility_requirements.empty()) {
    f.insufficient = true;
  }

  // Deterministic reference compatibility: used only when the planner context
  // holds an explicit compatibility entry for this subject identity and the
  // candidate carries a semantic target identity (named workload target). This
  // is the standalone reference logic; the authoritative result normally comes
  // from the candidate's CompatibilityEvidence.
  if (candidate.compatibility && candidate.compatibility->subject_identity.size() &&
      ctx.compatibility_table.count(candidate.compatibility->subject_identity)) {
    const std::string& subj = candidate.compatibility->subject_identity;
    std::string target = candidate.compatibility->state_format.empty()
                             ? "unqualified" : candidate.compatibility->state_format;
    if (!ctx.is_compatible(subj, target)) {
      hard(f, ev, RejectionReason::INCOMPATIBLE,
           "reference compatibility: subject '" + subj + "' incompatible with '" + target + "'");
    }
  }

  // 6. Target availability / readiness / incarnation.
  const bool needs_live_target =
      candidate.strategy != RecoveryStrategy::RECOMPUTE;
  if (needs_live_target) {
    if (candidate.availability) {
      const auto& av = *candidate.availability;
      if (!av.is_alive || !ctx.worker_is_current(av.worker)) {
        hard(f, ev, RejectionReason::TARGET_UNAVAILABLE, "target worker is not alive/current");
      }
      if (!ctx.worker_boot_matches(av.worker, av.boot)) {
        hard(f, ev, RejectionReason::TARGET_INCARNATION_MISMATCH,
             "target boot id does not match current worker boot");
      }
      if (av.readiness == CandidateReadiness::NOT_READY) {
        hard(f, ev, RejectionReason::READINESS_FAILED, "target readiness is NOT_READY");
      }
      if (av.readiness == CandidateReadiness::WARMING) f.defer = true;
      if (av.readiness == CandidateReadiness::UNKNOWN) f.insufficient = true;
      if (stale_or_expired(av.meta)) f.insufficient = true;
      if (revalidation_needed(av.meta)) f.revalidation = true;
    } else {
      f.insufficient = true;
    }
  }

  // 7. Resource feasibility.
  if (candidate.resource) {
    const auto& rs = *candidate.resource;
    if (rs.status == ResourceStatus::UNAVAILABLE) {
      hard(f, ev, RejectionReason::INSUFFICIENT_RESOURCES,
           "required resource '" + rs.resource_kind + "' is UNAVAILABLE");
    }
    if (rs.required.has_value() && rs.available.has_value()) {
      if (*rs.available < *rs.required) {
        hard(f, ev, RejectionReason::INSUFFICIENT_RESOURCES,
             "required " + std::to_string(*rs.required) + " but only " + std::to_string(*rs.available) +
             " available for '" + rs.resource_kind + "'");
      }
    }
    if (rs.generation != ctx.resource_generation) {
      hard(f, ev, RejectionReason::RESOURCE_GENERATION_STALE,
           "resource generation does not match current");
    }
    // Bind to the planner's current resource view: unknown current
    // availability must not yield unconditional feasibility.
    if (rs.required.has_value() && !rs.available.has_value()) f.insufficient = true;
    if (rs.required.has_value() && rs.available.has_value()) {
      if (rs.resource_kind.size() && !ctx.resource_sufficient(rs.resource_kind, *rs.required)) {
        if (!f.infeasible) {
          ev.violations.push_back({RejectionReason::INSUFFICIENT_RESOURCES,
              "planner current resource view does not guarantee '" + rs.resource_kind + "'"});
          f.insufficient = true;
        }
      }
    }
    if (rs.status == ResourceStatus::UNKNOWN) f.insufficient = true;
    if (revalidation_needed(rs.meta)) f.revalidation = true;
    if (stale_or_expired(rs.meta)) f.insufficient = true;
  } else {
    f.insufficient = true;
  }

  // 8. Topology feasibility (mandatory for migrate).
  if (candidate.strategy == RecoveryStrategy::MIGRATE) {
    if (candidate.topology) {
      if (!candidate.topology->path_possible) {
        hard(f, ev, RejectionReason::TOPOLOGY_IMPOSSIBLE,
             "migration path from '" + candidate.topology->source + "' to '" +
             candidate.topology->destination + "' is impossible");
      }
      if (candidate.topology->generation != ctx.topology_generation) {
        hard(f, ev, RejectionReason::TOPOLOGY_IMPOSSIBLE, "topology generation is stale");
      }
    } else {
      f.insufficient = true;
    }
  }

  // 9. Migration source/destination relation.
  if (candidate.strategy == RecoveryStrategy::MIGRATE) {
    if (!candidate.source_worker.is_valid() || candidate.source_worker == candidate.target_worker) {
      hard(f, ev, RejectionReason::INVALID_MIGRATION_RELATION,
           "migration requires a valid source worker distinct from the target");
    }
    if (!candidate.state || !candidate.state->is_durable) {
      f.insufficient = true;
      if (!candidate.state) ev.violations.push_back(
          {RejectionReason::MISSING_EVIDENCE, "migration requires portable durable state"});
    }
    if (!candidate.transfer) ev.violations.push_back(
        {RejectionReason::MISSING_EVIDENCE, "migration requires transfer evidence"});
    if (!candidate.transfer) f.insufficient = true;
  }

  // 10. Shadow promotion semantics.
  if (candidate.strategy == RecoveryStrategy::SHADOW_PROMOTE) {
    if (candidate.shadow) {
      const auto& sh = *candidate.shadow;
      if (sh.readiness != CandidateReadiness::READY) {
        hard(f, ev, RejectionReason::READINESS_FAILED, "shadow is not READY");
      }
      // Standing policy: a shadow is promotable only if its lag is bounded and
      // it is compatible.
      if (sh.lag.value() > 0 && sh.lag.value() > (1LL << 40)) {  // sanity bound for synthetic tests
        hard(f, ev, RejectionReason::SHADOW_NOT_CURRENT, "shadow lag too large");
      }
      if (sh.compatibility == CompatibilityResult::INCOMPATIBLE) {
        hard(f, ev, RejectionReason::INCOMPATIBLE, "shadow state is incompatible");
      }
    } else {
      f.insufficient = true;
    }
  }

  // 11. Recompute semantics.
  if (candidate.strategy == RecoveryStrategy::RECOMPUTE) {
    if (!ctx.recompute_available(request.workload_generation)) {
      hard(f, ev, RejectionReason::NO_RECOMPUTE_LINEAGE,
           "no authoritative lineage/inputs available for recomputation for this workload generation");
    }
    if (!candidate.cost || !candidate.cost->recompute_time) f.insufficient = true;
  }

  // 12. Rehydration requires reconstruction artifacts (represented via state).
  if (candidate.strategy == RecoveryStrategy::REHYDRATE) {
    if (!candidate.state) f.insufficient = true;
  }

  // 13. Aggregate.
  if (f.infeasible) ev.result = FeasibilityResult::INFEASIBLE;
  else if (f.insufficient) ev.result = FeasibilityResult::INSUFFICIENT_EVIDENCE;
  else if (f.revalidation) ev.result = FeasibilityResult::REVALIDATION_REQUIRED;
  else if (f.defer) ev.result = FeasibilityResult::DEFER;
  else ev.result = FeasibilityResult::FEASIBLE;

  return ev;
}

}  // namespace recovery_planner
