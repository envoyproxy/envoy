#include "common/stats/utility.h"

#include <algorithm>
#include <string>

#include "absl/strings/match.h"

namespace Envoy {
namespace Stats {

std::string Utility::sanitizeStatsName(absl::string_view name) {
  if (absl::EndsWith(name, ".")) {
    name.remove_suffix(1);
  }
  if (absl::StartsWith(name, ".")) {
    name.remove_prefix(1);
  }
  std::string stats_name = std::string(name);
  std::replace(stats_name.begin(), stats_name.end(), ':', '_');
  std::replace(stats_name.begin(), stats_name.end(), '\0', '_');
  return stats_name;
}

absl::optional<StatName> Utility::findTag(const Metric& metric, StatName find_tag_name) {
  absl::optional<StatName> value;
  metric.iterateTagStatNames(
      [&value, &find_tag_name](Stats::StatName tag_name, Stats::StatName tag_value) -> bool {
        if (tag_name == find_tag_name) {
          value = tag_value;
          return false;
        }
        return true;
      });
  return value;
}

std::string Utility::suffixedStatsName(absl::string_view name, Histogram::Unit unit) {
  std::string suffixed_name = std::string(name);

  if (unit != Histogram::Unit::Unspecified) {
    absl::StrAppend(&suffixed_name, "_", Utility::unitSymbol(unit));
  }

  return suffixed_name;
}

absl::string_view Utility::unitSymbol(Histogram::Unit unit) {
  switch (unit) {
  case Histogram::Unit::Unspecified:
    return "";
  case Histogram::Unit::Bytes:
    return "b";
  case Histogram::Unit::Microseconds:
    return "us";
  case Histogram::Unit::Milliseconds:
    return "ms";
  }

  ASSERT(0);
  return "unknown";
}

} // namespace Stats
} // namespace Envoy
