// From upstream Envoy
#include "common/stats/utility.h"

#include "library/common/data/utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Stats {
namespace Utility {

Stats::StatNameTagVector transformToStatNameTagVector(envoy_stats_tags tags,
                                                      Stats::StatNameSetPtr& stat_name_set) {
  Stats::StatNameTagVector transformed_tags;
  for (envoy_map_size_t i = 0; i < tags.length; i++) {
    std::string key(Data::Utility::copyToString(tags.entries[i].key));
    std::string val(Data::Utility::copyToString(tags.entries[i].value));

    transformed_tags.push_back({stat_name_set->add(key), stat_name_set->add(val)});
  }
  // The C envoy_stats_tags struct can be released now because the tags have been copied.
  release_envoy_stats_tags(tags);
  return transformed_tags;
}
} // namespace Utility
} // namespace Stats
} // namespace Envoy
