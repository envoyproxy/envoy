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
  const bool enable_per_opcode_request_bytes_metrics =
      proto_config.enable_per_opcode_request_bytes_metrics();
  const bool enable_per_opcode_response_bytes_metrics =
      proto_config.enable_per_opcode_response_bytes_metrics();
  const bool enable_per_opcode_decoder_error_metrics =
      proto_config.enable_per_opcode_decoder_error_metrics();
  const bool enable_latency_threshold_metrics = proto_config.enable_latency_threshold_metrics();
  const std::chrono::milliseconds default_latency_threshold(
      PROTOBUF_GET_MS_OR_DEFAULT(proto_config, default_latency_threshold, 100));
  const LatencyThresholdOverrideList& latency_threshold_overrides =
      proto_config.latency_threshold_overrides();

  // Check duplicated opcodes in config.
  absl::flat_hash_set<LatencyThresholdOverride_Opcode> opcodes;
  for (const auto& threshold_override : latency_threshold_overrides) {
    auto insertion = opcodes.insert(threshold_override.opcode());
    if (!insertion.second) {
      throw EnvoyException(fmt::format("Duplicated opcode find in config: {}",
                                       static_cast<uint32_t>(threshold_override.opcode())));
    }
  }

  ZooKeeperFilterConfigSharedPtr filter_config(std::make_shared<ZooKeeperFilterConfig>(
      stat_prefix, max_packet_bytes, enable_per_opcode_request_bytes_metrics,
      enable_per_opcode_response_bytes_metrics, enable_per_opcode_decoder_error_metrics,
      enable_latency_threshold_metrics, default_latency_threshold, latency_threshold_overrides,
      context.scope()));
  auto& time_source = context.serverFactoryContext().mainThreadDispatcher().timeSource();

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
