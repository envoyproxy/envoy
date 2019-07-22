#pragma once

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Captures a set of stat-names needed for recording during rate-limit
// filters. These should generally be initialized once per process, and
// not per-request, to avoid lock contention.
struct StatNames {
  explicit StatNames(Stats::SymbolTable& symbol_table)
      : pool_(symbol_table), ok_(pool_.add("ratelimit.ok")), error_(pool_.add("ratelimit.error")),
        failure_mode_allowed_(pool_.add("ratelimit.failure_mode_allowed")),
        over_limit_(pool_.add("ratelimit.over_limit")) {}
  Stats::StatNamePool pool_;
  Stats::StatName ok_;
  Stats::StatName error_;
  Stats::StatName failure_mode_allowed_;
  Stats::StatName over_limit_;
};

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
