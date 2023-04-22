#include "source/extensions/filters/network/zookeeper_proxy/config.h"

#include <string>

#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/zookeeper_proxy/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * Config registration for the ZooKeeper proxy filter. @see NamedNetworkFilterConfigFactory.
 */
Network::FilterFactoryCb ZooKeeperConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::zookeeper_proxy::v3::ZooKeeperProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("{}.zookeeper", proto_config.stat_prefix());
  const uint32_t max_packet_bytes =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, max_packet_bytes, 1024 * 1024);
  const LatencyThresholdList& latency_thresholds = proto_config.latency_thresholds();

  // Check duplicated opcodes in config.
  std::set<LatencyThreshold_Opcode> opcodes;
  for (const auto& threshold : latency_thresholds) {
    if (opcodes.find(threshold.opcode()) != opcodes.end()) {
      throw EnvoyException(fmt::format("Duplicated opcode find in config: {}",
                                       static_cast<uint32_t>(threshold.opcode())));
    }
    opcodes.insert(threshold.opcode());
  }

  ZooKeeperFilterConfigSharedPtr filter_config(std::make_shared<ZooKeeperFilterConfig>(
      stat_prefix, max_packet_bytes, latency_thresholds, context.scope()));
  auto& time_source = context.mainThreadDispatcher().timeSource();

  return [filter_config, &time_source](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<ZooKeeperFilter>(filter_config, time_source));
  };
}

/**
 * Static registration for the ZooKeeper proxy filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ZooKeeperConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
