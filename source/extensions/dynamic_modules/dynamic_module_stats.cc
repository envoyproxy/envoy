#include "source/extensions/dynamic_modules/dynamic_module_stats.h"

#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

void incrementLoadFailure(OptRef<Server::Configuration::CommonFactoryContext> context,
                          absl::string_view config_name, absl::string_view leaf) {

  if (!context.has_value()) {
    return;
  }

  const std::string name =
      Stats::Utility::sanitizeStatsName(config_name.empty() ? "default" : config_name);
  Stats::StatNameDynamicPool pool(context->scope().symbolTable());
  Stats::StatNameTagVector tags{{pool.add("config_name"), pool.add(name)}};
  Stats::Utility::counterFromElements(
      context->scope(), {Stats::DynamicName(DynamicModulesStatRoot), Stats::DynamicName(leaf)},
      tags)
      .inc();
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
