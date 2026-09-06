#pragma once

// Recovery Planner public umbrella header.
//
// Recovery Planner is a vendor-neutral C++20 runtime for authoritative recovery
// planning. It selects and fences recovery strategies across restore, restart,
// migrate, rehydrate, shadow-promotion, failover, and recomputation using state
// availability, compatibility, deadline, cost, resource, and recovery evidence.

#include "recovery_planner/types.hpp"
#include "recovery_planner/clock.hpp"
#include "recovery_planner/digest.hpp"
#include "recovery_planner/error.hpp"
#include "recovery_planner/strategy.hpp"
#include "recovery_planner/evidence.hpp"
#include "recovery_planner/request.hpp"
#include "recovery_planner/candidate.hpp"
#include "recovery_planner/planner_context.hpp"
#include "recovery_planner/feasibility.hpp"
#include "recovery_planner/economics.hpp"
#include "recovery_planner/plan.hpp"
#include "recovery_planner/persistence.hpp"
#include "recovery_planner/executor.hpp"
#include "recovery_planner/planner.hpp"
