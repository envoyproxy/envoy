#include "common/stats/utility.h"

#include <algorithm>
#include <string>

namespace Envoy {
namespace Stats {

std::string Utility::sanitizeStatsName(absl::string_view name) {
  std::string stats_name = std::string(name);
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  std::replace(stats_name.begin(), stats_name.end(), '\0', '_');
  return stats_name;
}

} // namespace Stats
} // namespace Envoy
