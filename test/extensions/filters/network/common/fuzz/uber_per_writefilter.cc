#include "extensions/filters/network/common/utility.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/extensions/filters/network/common/fuzz/uber_writefilter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {

std::vector<absl::string_view> UberWriteFilterFuzzer::filterNames() {
  // These filters have already been covered by this fuzzer.
  // Will extend to cover other network filters one by one.
  static std::vector<absl::string_view> filter_names;
  if (filter_names.empty()) {
    filter_names = {
        NetworkFilterNames::get().ZooKeeperProxy, // assert error in onWrite()
        NetworkFilterNames::get().KafkaBroker, 
        NetworkFilterNames::get().MongoProxy,
        NetworkFilterNames::get().MySQLProxy,
        // TODO(jianwendong) Add "NetworkFilterNames::get().Postgres" after its issues are fixed.
    };
  }
  return filter_names;
}

void UberWriteFilterFuzzer::perFilterSetup(const std::string& filter_name) {
  std::cout << filter_name << std::endl;
}

void UberWriteFilterFuzzer::checkInvalidInputForFuzzer(const std::string& filter_name,
                                                       Protobuf::Message*) {
  // System calls such as reading files are prohibited in this fuzzer. Some input that crashes the
  // mock/fake objects are also prohibited. For now there are only two filters {DirectResponse,
  // LocalRateLimit} on which we have constraints.
  const std::string name = Extensions::NetworkFilters::Common::FilterNameUtil::canonicalFilterName(
      std::string(filter_name));
  std::cout << "check:" << name << std::endl;
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
