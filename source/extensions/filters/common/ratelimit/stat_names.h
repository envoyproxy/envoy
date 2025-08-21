#pragma once

#include "source/common/common/empty_string.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Captures a set of stat-names needed for recording during rate-limit
// filters. These should generally be initialized once per process, and
// not per-request, to avoid lock contention.
struct StatNames {
  explicit StatNames(Stats::SymbolTable& symbol_table,
                     const std::string& stat_prefix = EMPTY_STRING)
      : pool_(symbol_table), ok_(pool_.add(createPoolStatName(stat_prefix, "ok"))),
        error_(pool_.add(createPoolStatName(stat_prefix, "error"))),
        failure_mode_allowed_(pool_.add(createPoolStatName(stat_prefix, "failure_mode_allowed"))),
        over_limit_(pool_.add(createPoolStatName(stat_prefix, "over_limit"))) {}

  // This generates ratelimit.<optional stat_prefix>.name
  const std::string createPoolStatName(const std::string& stat_prefix, const std::string& name) {
    return absl::StrCat("ratelimit",
                        stat_prefix.empty() ? EMPTY_STRING : absl::StrCat(".", stat_prefix), ".",
                        name);
  }
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
