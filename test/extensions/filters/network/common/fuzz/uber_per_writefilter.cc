#include "source/extensions/filters/network/common/utility.h"
#include "source/extensions/filters/network/kafka/broker/config.h"
#include "source/extensions/filters/network/mongo_proxy/config.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_config.h"
#include "source/extensions/filters/network/zookeeper_proxy/config.h"

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
        ZooKeeperProxy::ZooKeeperProxyName, Kafka::Broker::KafkaBrokerName,
        MongoProxy::MongoProxyName, MySQLProxy::MySQLProxyName
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

} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
