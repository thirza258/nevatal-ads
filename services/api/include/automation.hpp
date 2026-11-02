#pragma once

#include "db.hpp"

namespace adcontrol {

// Advances schedules, evaluates performance triggers, and executes a bounded
// number of queued workflow runs. Safe to call repeatedly from the maintenance
// worker; idempotency keys prevent duplicate runs.
int run_automation_tick(Database& db, int max_runs = 20);

} // namespace adcontrol
