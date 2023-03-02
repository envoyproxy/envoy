#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/network/common/fuzz/uber_writefilter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
std::vector<absl::string_view> UberWriteFilterFuzzer::filterNames() {
  // These filters have already been covered by this fuzzer.
  // Will extend to cover other network filters one by one.
  static const std::vector<std::string> supported_filter_names = {{{FILTERS}}};
  static std::vector<absl::string_view> filter_names;
  if (filter_names.empty()) {
    const auto factories = Registry::FactoryRegistry<
        Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
    for (auto& filter_name : supported_filter_names) {
      if (factories.contains(filter_name)) {
        filter_names.push_back(filter_name);
      } else {
        ENVOY_LOG_MISC(debug, "Filter name not found in the factory: {}", filter_name);
      }
    }
  }
  return filter_names;
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
