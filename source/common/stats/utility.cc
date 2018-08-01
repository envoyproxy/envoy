#include "common/stats/utility.h"

#include <algorithm>
#include <string>

namespace Envoy {
namespace Stats {

std::string Utility::sanitizeStatsName(const std::string& name) {
  std::string stats_name = name;
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  std::replace(stats_name.begin(), stats_name.end(), '\0', '_');
  return stats_name;
}

} // namespace Stats
} // namespace Envoy
