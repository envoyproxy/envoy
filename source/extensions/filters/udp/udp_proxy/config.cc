#include "source/extensions/filters/udp/udp_proxy/config.h"

#include "source/common/formatter/substitution_format_string.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

constexpr uint32_t DefaultMaxConnectAttempts = 1;
constexpr uint32_t DefaultMaxBufferedDatagrams = 1024;
constexpr uint64_t DefaultMaxBufferedBytes = 16384;

ProtobufTypes::MessagePtr TunnelResponseHeadersOrTrailers::serializeAsProto() const {
  auto proto_out = std::make_unique<envoy::config::core::v3::HeaderMap>();
  value().iterate([&proto_out](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    auto* new_header = proto_out->add_headers();
    new_header->set_key(std::string(e.key().getStringView()));
    new_header->set_value(std::string(e.value().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  });
  return proto_out;
}

const std::string& TunnelResponseHeaders::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.udp_proxy.propagate_response_headers");
}

const std::string& TunnelResponseTrailers::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.udp_proxy.propagate_response_trailers");
}

TunnelingConfigImpl::TunnelingConfigImpl(const TunnelingConfig& config,
                                         Server::Configuration::FactoryContext& context)
    : header_parser_(Envoy::Router::HeaderParser::configure(config.headers_to_add())),
      proxy_port_(), target_port_(config.default_target_port()), use_post_(config.use_post()),
      post_path_(config.post_path()),
      max_connect_attempts_(config.has_retry_options()
                                ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.retry_options(),
                                                                  max_connect_attempts,
                                                                  DefaultMaxConnectAttempts)
                                : DefaultMaxConnectAttempts),
      buffer_enabled_(config.has_buffer_options()),
      max_buffered_datagrams_(config.has_buffer_options()
                                  ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.buffer_options(),
                                                                    max_buffered_datagrams,
                                                                    DefaultMaxBufferedDatagrams)
                                  : DefaultMaxBufferedDatagrams),
      max_buffered_bytes_(config.has_buffer_options()
                              ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.buffer_options(),
                                                                max_buffered_bytes,
                                                                DefaultMaxBufferedBytes)
                              : DefaultMaxBufferedBytes),
      propagate_response_headers_(config.propagate_response_headers()),
      propagate_response_trailers_(config.propagate_response_trailers()) {
  if (!post_path_.empty() && !use_post_) {
    throw EnvoyException("Can't set a post path when POST method isn't used");
  }

  if (post_path_.empty()) {
    post_path_ = "/";
  } else if (post_path_.rfind("/", 0) != 0) {
    throw EnvoyException("Path must start with '/'");
  }

  envoy::config::core::v3::SubstitutionFormatString proxy_substitution_format_config;
  proxy_substitution_format_config.mutable_text_format_source()->set_inline_string(
      config.proxy_host());
  proxy_host_formatter_ = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
      proxy_substitution_format_config, context);

  if (config.has_proxy_port()) {
    uint32_t port = config.proxy_port().value();
    if (port == 0 || port > 65535) {
      throw EnvoyException("Port value not in range");
    }

    proxy_port_ = port;
  }

  envoy::config::core::v3::SubstitutionFormatString target_substitution_format_config;
  target_substitution_format_config.mutable_text_format_source()->set_inline_string(
      config.target_host());
  target_host_formatter_ = Formatter::SubstitutionFormatStringUtils::fromProtoConfig(
      target_substitution_format_config, context);
}

UdpProxyFilterConfigImpl::UdpProxyFilterConfigImpl(
    Server::Configuration::ListenerFactoryContext& context,
    const envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig& config)
    : cluster_manager_(context.serverFactoryContext().clusterManager()),
      time_source_(context.serverFactoryContext().timeSource()),
      router_(std::make_shared<Router::RouterImpl>(config, context.serverFactoryContext())),
      session_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, idle_timeout, 60 * 1000)),
      use_original_src_ip_(config.use_original_src_ip()),
      use_per_packet_load_balancing_(config.use_per_packet_load_balancing()),
      stats_(generateStats(config.stat_prefix(), context.scope())),
      // Default prefer_gro to true for upstream client traffic.
      upstream_socket_config_(config.upstream_socket_config(), true),
      random_generator_(context.serverFactoryContext().api().randomGenerator()) {
  if (use_per_packet_load_balancing_ && config.has_tunneling_config()) {
    throw EnvoyException(
        "Only one of use_per_packet_load_balancing or tunneling_config can be used.");
  }

  if (use_per_packet_load_balancing_ && !config.session_filters().empty()) {
    throw EnvoyException(
        "Only one of use_per_packet_load_balancing or session_filters can be used.");
  }

  if (use_original_src_ip_ &&
      !Api::OsSysCallsSingleton::get().supportsIpTransparent(
          context.serverFactoryContext().options().localAddressIpVersion())) {
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

  if (config.has_tunneling_config()) {
    tunneling_config_ = std::make_unique<TunnelingConfigImpl>(config.tunneling_config(), context);
  }

  if (config.has_access_log_options()) {
    flush_access_log_on_tunnel_connected_ =
        config.access_log_options().flush_access_log_on_tunnel_connected();

    if (config.access_log_options().has_access_log_flush_interval()) {
      const uint64_t flush_interval = DurationUtil::durationToMilliseconds(
          config.access_log_options().access_log_flush_interval());
      access_log_flush_interval_ = std::chrono::milliseconds(flush_interval);
    }
  } else {
    flush_access_log_on_tunnel_connected_ = false;
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
