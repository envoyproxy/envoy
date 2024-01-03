#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/udp/udp_proxy/udp_proxy_filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

using TunnelingConfig =
    envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig::UdpTunnelingConfig;

/**
 * Base class for both tunnel response headers and trailers.
 */
class TunnelResponseHeadersOrTrailers : public StreamInfo::FilterState::Object {
public:
  ProtobufTypes::MessagePtr serializeAsProto() const override;
  virtual const Http::HeaderMap& value() const PURE;
};

/**
 * Response headers for the tunneling connections.
 */
class TunnelResponseHeaders : public TunnelResponseHeadersOrTrailers {
public:
  TunnelResponseHeaders(Http::ResponseHeaderMapPtr&& response_headers)
      : response_headers_(std::move(response_headers)) {}
  const Http::HeaderMap& value() const override { return *response_headers_; }
  static const std::string& key();

private:
  const Http::ResponseHeaderMapPtr response_headers_;
};

/**
 * Response trailers for the tunneling connections.
 */
class TunnelResponseTrailers : public TunnelResponseHeadersOrTrailers {
public:
  TunnelResponseTrailers(Http::ResponseTrailerMapPtr&& response_trailers)
      : response_trailers_(std::move(response_trailers)) {}
  const Http::HeaderMap& value() const override { return *response_trailers_; }
  static const std::string& key();

private:
  const Http::ResponseTrailerMapPtr response_trailers_;
};

class TunnelingConfigImpl : public UdpTunnelingConfig {
public:
  TunnelingConfigImpl(const TunnelingConfig& config,
                      Server::Configuration::FactoryContext& context);

  const std::string proxyHost(const StreamInfo::StreamInfo& stream_info) const override {
    return proxy_host_formatter_->formatWithContext({}, stream_info);
  }

  const std::string targetHost(const StreamInfo::StreamInfo& stream_info) const override {
    return target_host_formatter_->formatWithContext({}, stream_info);
  }

  const absl::optional<uint32_t>& proxyPort() const override { return proxy_port_; };
  uint32_t defaultTargetPort() const override { return target_port_; };
  bool usePost() const override { return use_post_; };
  const std::string& postPath() const override { return post_path_; }
  Http::HeaderEvaluator& headerEvaluator() const override { return *header_parser_; };
  uint32_t maxConnectAttempts() const override { return max_connect_attempts_; };
  bool bufferEnabled() const override { return buffer_enabled_; };
  uint32_t maxBufferedDatagrams() const override { return max_buffered_datagrams_; };
  uint64_t maxBufferedBytes() const override { return max_buffered_bytes_; };

  void
  propagateResponseHeaders(Http::ResponseHeaderMapPtr&& headers,
                           const StreamInfo::FilterStateSharedPtr& filter_state) const override {
    if (!propagate_response_headers_) {
      return;
    }

    filter_state->setData(TunnelResponseHeaders::key(),
                          std::make_shared<TunnelResponseHeaders>(std::move(headers)),
                          StreamInfo::FilterState::StateType::ReadOnly,
                          StreamInfo::FilterState::LifeSpan::Connection);
  }

  void
  propagateResponseTrailers(Http::ResponseTrailerMapPtr&& trailers,
                            const StreamInfo::FilterStateSharedPtr& filter_state) const override {
    if (!propagate_response_trailers_) {
      return;
    }

    filter_state->setData(TunnelResponseTrailers::key(),
                          std::make_shared<TunnelResponseTrailers>(std::move(trailers)),
                          StreamInfo::FilterState::StateType::ReadOnly,
                          StreamInfo::FilterState::LifeSpan::Connection);
  }

private:
  std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;
  Formatter::FormatterPtr proxy_host_formatter_;
  absl::optional<uint32_t> proxy_port_;
  Formatter::FormatterPtr target_host_formatter_;
  const uint32_t target_port_;
  bool use_post_;
  std::string post_path_;
  const uint32_t max_connect_attempts_;
  bool buffer_enabled_;
  uint32_t max_buffered_datagrams_;
  uint64_t max_buffered_bytes_;
  bool propagate_response_headers_;
  bool propagate_response_trailers_;
};

class UdpProxyFilterConfigImpl : public UdpProxyFilterConfig,
                                 public FilterChainFactory,
                                 Logger::Loggable<Logger::Id::config> {
public:
  UdpProxyFilterConfigImpl(
      Server::Configuration::ListenerFactoryContext& context,
      const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config);

  // UdpProxyFilterConfig
  const std::string route(const Network::Address::Instance& destination_address,
                          const Network::Address::Instance& source_address) const override {
    return router_->route(destination_address, source_address);
  }
  const std::vector<std::string>& allClusterNames() const override {
    return router_->allClusterNames();
  }
  Upstream::ClusterManager& clusterManager() const override { return cluster_manager_; }
  std::chrono::milliseconds sessionTimeout() const override { return session_timeout_; }
  bool usingOriginalSrcIp() const override { return use_original_src_ip_; }
  bool usingPerPacketLoadBalancing() const override { return use_per_packet_load_balancing_; }
  const Udp::HashPolicy* hashPolicy() const override { return hash_policy_.get(); }
  UdpProxyDownstreamStats& stats() const override { return stats_; }
  TimeSource& timeSource() const override { return time_source_; }
  const Network::ResolvedUdpSocketConfig& upstreamSocketConfig() const override {
    return upstream_socket_config_;
  }
  const std::vector<AccessLog::InstanceSharedPtr>& sessionAccessLogs() const override {
    return session_access_logs_;
  }
  const std::vector<AccessLog::InstanceSharedPtr>& proxyAccessLogs() const override {
    return proxy_access_logs_;
  }
  const FilterChainFactory& sessionFilterFactory() const override { return *this; };
  bool hasSessionFilters() const override { return !filter_factories_.empty(); }
  const UdpTunnelingConfigPtr& tunnelingConfig() const override { return tunneling_config_; };
  bool flushAccessLogOnTunnelConnected() const override {
    return flush_access_log_on_tunnel_connected_;
  }
  const absl::optional<std::chrono::milliseconds>& accessLogFlushInterval() const override {
    return access_log_flush_interval_;
  }
  Random::RandomGenerator& randomGenerator() const override { return random_generator_; }

  // FilterChainFactory
  void createFilterChain(FilterChainFactoryCallbacks& callbacks) const override {
    for (const FilterFactoryCb& factory : filter_factories_) {
      factory(callbacks);
    }
  };

private:
  static UdpProxyDownstreamStats generateStats(const std::string& stat_prefix,
                                               Stats::Scope& scope) {
    const auto final_prefix = absl::StrCat("udp.", stat_prefix);
    return {ALL_UDP_PROXY_DOWNSTREAM_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                           POOL_GAUGE_PREFIX(scope, final_prefix))};
  }

  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
  Router::RouterConstSharedPtr router_;
  const std::chrono::milliseconds session_timeout_;
  const bool use_original_src_ip_;
  const bool use_per_packet_load_balancing_;
  bool flush_access_log_on_tunnel_connected_;
  absl::optional<std::chrono::milliseconds> access_log_flush_interval_;
  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  mutable UdpProxyDownstreamStats stats_;
  const Network::ResolvedUdpSocketConfig upstream_socket_config_;
  std::vector<AccessLog::InstanceSharedPtr> session_access_logs_;
  std::vector<AccessLog::InstanceSharedPtr> proxy_access_logs_;
  UdpTunnelingConfigPtr tunneling_config_;
  std::list<SessionFilters::FilterFactoryCb> filter_factories_;
  Random::RandomGenerator& random_generator_;
};

/**
 * Config registration for the UDP proxy filter. @see NamedUdpListenerFilterConfigFactory.
 */
class UdpProxyFilterConfigFactory
    : public Server::Configuration::NamedUdpListenerFilterConfigFactory {
public:
  // NamedUdpListenerFilterConfigFactory
  Network::UdpListenerFilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message& config,
                               Server::Configuration::ListenerFactoryContext& context) override {
    auto shared_config = std::make_shared<UdpProxyFilterConfigImpl>(
        context, MessageUtil::downcastAndValidate<
                     const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig&>(
                     config, context.messageValidationVisitor()));
    return [shared_config](Network::UdpListenerFilterManager& filter_manager,
                           Network::UdpReadFilterCallbacks& callbacks) -> void {
      filter_manager.addReadFilter(std::make_unique<UdpProxyFilter>(callbacks, shared_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig>();
  }

  std::string name() const override { return "envoy.filters.udp_listener.udp_proxy"; }
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
