#include "common/stats/utility.h"

#include <algorithm>
#include <string>
#include <regex>

namespace Envoy {
namespace Stats {

const std::regex re("[^a-zA-Z0-9_]");

std::string Utility::sanitizeStatsName(const std::string& name) {
  return std::regex_replace(name, re, "_");
}

} // namespace Stats
} // namespace Envoy
