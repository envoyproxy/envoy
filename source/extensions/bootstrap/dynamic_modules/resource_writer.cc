#include "source/extensions/bootstrap/dynamic_modules/resource_writer.h"

#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/dynamic_module_stats.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

WriterStats generateWriterStats(Stats::Scope& scope, absl::string_view config_name,
                                const WriterStatNames& names) {
  Stats::StatNameDynamicPool pool(scope.symbolTable());
  const Stats::StatNameTagVector tags{
      {pool.add("config_name"), pool.add(Stats::Utility::sanitizeStatsName(config_name))}};
  const auto elements = [&names](absl::string_view name) -> Stats::ElementVec {
    return {Stats::DynamicName(Extensions::DynamicModules::DynamicModulesStatRoot),
            Stats::DynamicName(names.prefix), Stats::DynamicName(name)};
  };
  const auto counter = [&](absl::string_view name) -> Stats::Counter& {
    return Stats::Utility::counterFromElements(scope, elements(name), tags);
  };
  const auto gauge = [&](absl::string_view name) -> Stats::Gauge& {
    return Stats::Utility::gaugeFromElements(scope, elements(name),
                                             Stats::Gauge::ImportMode::NeverImport, tags);
  };
  return WriterStats{
      counter("update_submitted"),      counter("update_rejected"), counter("update_active"),
      counter("update_superseded"),     counter("update_removed"),  counter("update_failed"),
      counter("update_ownership_lost"), counter("ownership_lost"),  gauge(names.owned),
      gauge("updates_pending")};
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
