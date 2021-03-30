#pragma once

// From upstream Envoy
#include "common/stats/utility.h"

#include "library/common/types/c_types.h"

namespace Envoy {
namespace Stats {
namespace Utility {

/**
 * Transforms from envoy_stats_tags to Stats::StatNameTagVector.
 *
 * @param envoy_stats_tags tags to be transformed. tags is free'd. Use after function return is
 * unsafe.
 * @param stat_name_set the Stats::StatNameSetPtr for the transformed Stats::StatNameTagVector to be
 * kept in.
 * @return Stats::StatNameTagVector within the given stat_name_set.
 */
Stats::StatNameTagVector transformToStatNameTagVector(envoy_stats_tags tags,
                                                      Stats::StatNameSetPtr& stat_name_set);
} // namespace Utility
} // namespace Stats
} // namespace Envoy
