#include "source/extensions/filters/udp/udp_proxy/config.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

UdpProxyFilterConfigImpl::UdpProxyFilterConfigImpl(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config)
    : cluster_manager_(context.clusterManager()), time_source_(context.timeSource()),
      router_(std::make_shared<Router::RouterImpl>(config, context.getServerFactoryContext())),
      session_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, idle_timeout, 60 * 1000)),
      use_original_src_ip_(config.use_original_src_ip()),
      use_per_packet_load_balancing_(config.use_per_packet_load_balancing()),
      stats_(generateStats(config.stat_prefix(), context.scope())),
      // Default prefer_gro to true for upstream client traffic.
      upstream_socket_config_(config.upstream_socket_config(), true),
      random_(context.api().randomGenerator()) {
  if (use_original_src_ip_ &&
      !Api::OsSysCallsSingleton::get().supportsIpTransparent(
          context.getServerFactoryContext().options().localAddressIpVersion())) {
    ExceptionUtil::throwEnvoyException(
        "The platform does not support either IP_TRANSPARENT or IPV6_TRANSPARENT. Or the envoy "
        "is not running with the CAP_NET_ADMIN capability.");
  }

  session_access_logs_.reserve(config.access_log_size());
  for (const envoy::config::accesslog::v3::AccessLog& log_config : config.access_log()) {
    session_access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(log_config, context));
  }

  proxy_access_logs_.reserve(config.proxy_access_log_size());
  for (const envoy::config::accesslog::v3::AccessLog& log_config : config.proxy_access_log()) {
    proxy_access_logs_.emplace_back(AccessLog::AccessLogFactory::fromProto(log_config, context));
  }

  if (!config.hash_policies().empty()) {
    hash_policy_ = std::make_unique<HashPolicyImpl>(config.hash_policies());
  }

  for (const auto& filter : config.session_filters()) {
    ENVOY_LOG(debug, "    UDP session filter #{}", filter_factories_.size());
    ENVOY_LOG(debug, "      name: {}", filter.name());
    ENVOY_LOG(debug, "    config: {}",
              MessageUtil::getJsonStringFromMessageOrError(
                  static_cast<const Protobuf::Message&>(filter.typed_config()), true));

    auto& factory = Config::Utility::getAndCheckFactory<NamedUdpSessionFilterConfigFactory>(filter);
    ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
        filter, context.messageValidationVisitor(), factory);
    FilterFactoryCb callback = factory.createFilterFactoryFromProto(*message, context);
    filter_factories_.push_back(callback);
  }
}

static Registry::RegisterFactory<UdpProxyFilterConfigFactory,
                                 Server::Configuration::NamedUdpListenerFilterConfigFactory>
    register_;

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
