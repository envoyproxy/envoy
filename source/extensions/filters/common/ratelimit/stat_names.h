#pragma once

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

struct StatNames {
  StatNames(Stats::StatNamePool& pool)
      : ok_(pool.add("ratelimit.ok")), error_(pool.add("ratelimit.error")),
        failure_mode_allowed_(pool.add("ratelimit.failure_mode_allowed")),
        over_limit_(pool.add("ratelimit.over_limit")) {}
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
