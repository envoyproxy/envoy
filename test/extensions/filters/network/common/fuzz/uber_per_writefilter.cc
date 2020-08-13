#include "envoy/extensions/filters/network/mongo_proxy/v3/mongo_proxy.pb.h"

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
    const auto factories = Registry::FactoryRegistry<
        Server::Configuration::NamedNetworkFilterConfigFactory>::factories();
    const std::vector<absl::string_view> supported_filter_names = {
        NetworkFilterNames::get().ZooKeeperProxy, NetworkFilterNames::get().KafkaBroker,
        NetworkFilterNames::get().MongoProxy, NetworkFilterNames::get().MySQLProxy
        // TODO(jianwendong) Add "NetworkFilterNames::get().Postgres" after it supports untrusted
        // data.
    };
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

void UberWriteFilterFuzzer::checkInvalidInputForFuzzer(const std::string& filter_name,
                                                       Protobuf::Message* config_message) {
  // System calls such as reading files are prohibited in this fuzzer. Some inputs that crash the
  // mock/fake objects are also prohibited. We could also avoid fuzzing some unfinished features by
  // checking them here. For now there is only one filter {MongoProxy} on which we have a
  // constraint.
  const std::string name = Extensions::NetworkFilters::Common::FilterNameUtil::canonicalFilterName(
      std::string(filter_name));
  if (filter_name == NetworkFilterNames::get().MongoProxy) {
    envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy& config =
        dynamic_cast<envoy::extensions::filters::network::mongo_proxy::v3::MongoProxy&>(
            *config_message);
    if (config.has_delay() && config.mutable_delay()->has_header_delay()) {
      // MongoProxy filter doesn't allow header_delay because it will pass nullptr to percentage()
      // which will cause "runtime error: member call on null pointer". (See:
      // https://github.com/envoyproxy/envoy/blob/master/source/extensions/filters/network/mongo_proxy/proxy.cc#L403
      // and
      // https://github.com/envoyproxy/envoy/blob/master/source/extensions/filters/common/fault/fault_config.cc#L16)
      throw EnvoyException(absl::StrCat(
          "header delay is not supported in the config of a MongoProxy filter. Config:\n{}",
          config.DebugString()));
    }
  }
}

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
