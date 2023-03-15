#include "source/extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/network/common/fuzz/uber_writefilter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
std::vector<absl::string_view> UberWriteFilterFuzzer::filterNames() {
  // These filters have already been covered by this fuzzer.
  // Will extend to cover other network filters one by one.
  static std::vector<absl::string_view> filter_names;
  if (filter_names.empty()) {
    filter_names = Registry::FactoryRegistry<
        Server::Configuration::NamedNetworkFilterConfigFactory>::registeredNames();
  }
  return filter_names;
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
