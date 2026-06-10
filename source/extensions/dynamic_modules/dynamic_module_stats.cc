#include "source/extensions/dynamic_modules/dynamic_module_stats.h"

#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

void incrementLoadFailure(Stats::Scope& scope, absl::string_view config_name,
                          absl::string_view leaf) {
  const std::string name =
      Stats::Utility::sanitizeStatsName(config_name.empty() ? "default" : config_name);
  Stats::StatNameDynamicPool pool(scope.symbolTable());
  Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add(name)}};
  Stats::Utility::counterFromElements(
      scope, {Stats::DynamicName(DynamicModulesStatRoot), Stats::DynamicName(leaf)}, tags)
      .inc();
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
