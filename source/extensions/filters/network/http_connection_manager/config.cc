#include "extensions/filters/network/http_connection_manager/config.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/admin.h"
#include "envoy/tracing/http_tracer.h"

#include "common/access_log/access_log_impl.h"
#include "common/common/fmt.h"
#include "common/config/filter_json.h"
#include "common/config/utility.h"
#include "common/http/conn_manager_utility.h"
#include "common/http/date_provider_impl.h"
#include "common/http/default_server_string.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/utility.h"
#include "common/router/rds_impl.h"
#include "common/router/scoped_rds.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {
namespace {

using FilterFactoriesList = std::list<Http::FilterFactoryCb>;
using FilterFactoryMap = std::map<std::string, HttpConnectionManagerConfig::FilterConfig>;

HttpConnectionManagerConfig::UpgradeMap::const_iterator
findUpgradeBoolCaseInsensitive(const HttpConnectionManagerConfig::UpgradeMap& upgrade_map,
                               absl::string_view upgrade_type) {
  for (auto it = upgrade_map.begin(); it != upgrade_map.end(); ++it) {
    if (StringUtil::CaseInsensitiveCompare()(it->first, upgrade_type)) {
      return it;
    }
  }
  return upgrade_map.end();
}

FilterFactoryMap::const_iterator findUpgradeCaseInsensitive(const FilterFactoryMap& upgrade_map,
                                                            absl::string_view upgrade_type) {
  for (auto it = upgrade_map.begin(); it != upgrade_map.end(); ++it) {
    if (StringUtil::CaseInsensitiveCompare()(it->first, upgrade_type)) {
      return it;
    }
  }
  return upgrade_map.end();
}

std::unique_ptr<Http::InternalAddressConfig> createInternalAddressConfig(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        config) {
  if (config.has_internal_address_config()) {
    return std::make_unique<InternalAddressConfig>(config.internal_address_config());
  }

  return std::make_unique<Http::DefaultInternalAddressConfig>();
}

} // namespace

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(date_provider);
SINGLETON_MANAGER_REGISTRATION(route_config_provider_manager);
SINGLETON_MANAGER_REGISTRATION(scoped_routes_config_provider_manager);

Network::FilterFactoryCb
HttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Http::TlsCachingDateProviderImpl> date_provider =
      context.singletonManager().getTyped<Http::TlsCachingDateProviderImpl>(
          SINGLETON_MANAGER_REGISTERED_NAME(date_provider), [&context] {
            return std::make_shared<Http::TlsCachingDateProviderImpl>(context.dispatcher(),
                                                                      context.threadLocal());
          });

  std::shared_ptr<Router::RouteConfigProviderManager> route_config_provider_manager =
      context.singletonManager().getTyped<Router::RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(route_config_provider_manager), [&context] {
            return std::make_shared<Router::RouteConfigProviderManagerImpl>(context.admin());
          });

  std::shared_ptr<Router::ScopedRoutesConfigProviderManager> scoped_routes_config_provider_manager =
      context.singletonManager().getTyped<Router::ScopedRoutesConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(scoped_routes_config_provider_manager),
          [&context, route_config_provider_manager] {
            return std::make_shared<Router::ScopedRoutesConfigProviderManager>(
                context.admin(), *route_config_provider_manager);
          });

  std::shared_ptr<HttpConnectionManagerConfig> filter_config(new HttpConnectionManagerConfig(
      proto_config, context, *date_provider, *route_config_provider_manager,
      *scoped_routes_config_provider_manager));

  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count.
  // Keep in mind the lambda capture list **doesn't** determine the destruction order, but it's fine
  // as these captured objects are also global singletons.
  return [scoped_routes_config_provider_manager, route_config_provider_manager, date_provider,
          filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
        *filter_config, context.drainDecision(), context.random(), context.httpContext(),
        context.runtime(), context.localInfo(), context.clusterManager(),
        &context.overloadManager(), context.dispatcher().timeSource())});
  };
}

Network::FilterFactoryCb HttpConnectionManagerFilterConfigFactory::createFilterFactory(
    const Json::Object& json_config, Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager proto_config;
  Config::FilterJson::translateHttpConnectionManager(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, context);
}

/**
 * Static registration for the HTTP connection manager filter.
 */
REGISTER_FACTORY(HttpConnectionManagerFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

InternalAddressConfig::InternalAddressConfig(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
        InternalAddressConfig& config)
    : unix_sockets_(config.unix_sockets()) {}

HttpConnectionManagerConfig::HttpConnectionManagerConfig(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        config,
    Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
    Router::RouteConfigProviderManager& route_config_provider_manager,
    Config::ConfigProviderManager& scoped_routes_config_provider_manager)
    : context_(context), stats_prefix_(fmt::format("http.{}.", config.stat_prefix())),
      stats_(Http::ConnectionManagerImpl::generateStats(stats_prefix_, context_.scope())),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats(stats_prefix_, context_.scope())),
      use_remote_address_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_remote_address, false)),
      internal_address_config_(createInternalAddressConfig(config)),
      xff_num_trusted_hops_(config.xff_num_trusted_hops()),
      skip_xff_append_(config.skip_xff_append()), via_(config.via()),
      route_config_provider_manager_(route_config_provider_manager),
      scoped_routes_config_provider_manager_(scoped_routes_config_provider_manager),
      http2_settings_(Http::Utility::parseHttp2Settings(config.http2_protocol_options())),
      http1_settings_(Http::Utility::parseHttp1Settings(config.http_protocol_options())),
      max_request_headers_kb_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config, max_request_headers_kb, Http::DEFAULT_MAX_REQUEST_HEADERS_KB)),
      max_request_headers_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.common_http_protocol_options(), max_headers_count,
          context.runtime().snapshot().getInteger(Http::MaxRequestHeadersCountOverrideKey,
                                                  Http::DEFAULT_MAX_HEADERS_COUNT))),
      idle_timeout_(PROTOBUF_GET_OPTIONAL_MS(config.common_http_protocol_options(), idle_timeout)),
      stream_idle_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, stream_idle_timeout, StreamIdleTimeoutMs)),
      request_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, request_timeout, RequestTimeoutMs)),
      drain_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, drain_timeout, 5000)),
      generate_request_id_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, generate_request_id, true)),
      preserve_external_request_id_(config.preserve_external_request_id()),
      date_provider_(date_provider),
      listener_stats_(Http::ConnectionManagerImpl::generateListenerStats(stats_prefix_,
                                                                         context_.listenerScope())),
      proxy_100_continue_(config.proxy_100_continue()),
      delayed_close_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, delayed_close_timeout, 1000)),
      normalize_path_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config, normalize_path,
          // TODO(htuch): we should have a boolean variant of featureEnabled() here.
          context.runtime().snapshot().featureEnabled("http_connection_manager.normalize_path",
#ifdef ENVOY_NORMALIZE_PATH_BY_DEFAULT
                                                      100
#else
                                                      0
#endif
                                                      ))),
      merge_slashes_(config.merge_slashes()) {
  // If idle_timeout_ was not configured in common_http_protocol_options, use value in deprecated
  // idle_timeout field.
  // TODO(asraa): Remove when idle_timeout is removed.
  if (!idle_timeout_) {
    idle_timeout_ = PROTOBUF_GET_OPTIONAL_MS(config, idle_timeout);
  }
  if (!idle_timeout_) {
    idle_timeout_ = std::chrono::hours(1);
  } else if (idle_timeout_.value().count() == 0) {
    idle_timeout_ = absl::nullopt;
  }

  // If scoped RDS is enabled, avoid creating a route config provider. Route config providers will
  // be managed by the scoped routing logic instead.
  switch (config.route_specifier_case()) {
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::kRds:
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      kRouteConfig:
    route_config_provider_ = Router::RouteConfigProviderUtil::create(
        config, context_, stats_prefix_, route_config_provider_manager_);
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      kScopedRoutes:
    scoped_routes_config_provider_ = Router::ScopedRoutesConfigProviderUtil::create(
        config, context_, stats_prefix_, scoped_routes_config_provider_manager_);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  switch (config.forward_client_cert_details()) {
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::SANITIZE:
    forward_client_cert_ = Http::ForwardClientCertType::Sanitize;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      FORWARD_ONLY:
    forward_client_cert_ = Http::ForwardClientCertType::ForwardOnly;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      APPEND_FORWARD:
    forward_client_cert_ = Http::ForwardClientCertType::AppendForward;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      SANITIZE_SET:
    forward_client_cert_ = Http::ForwardClientCertType::SanitizeSet;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      ALWAYS_FORWARD_ONLY:
    forward_client_cert_ = Http::ForwardClientCertType::AlwaysForwardOnly;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const auto& set_current_client_cert_details = config.set_current_client_cert_details();
  if (set_current_client_cert_details.cert()) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::Cert);
  }
  if (set_current_client_cert_details.chain()) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::Chain);
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(set_current_client_cert_details, subject, false)) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::Subject);
  }
  if (set_current_client_cert_details.uri()) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::URI);
  }
  if (set_current_client_cert_details.dns()) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::DNS);
  }

  if (config.has_add_user_agent() && config.add_user_agent().value()) {
    user_agent_ = context_.localInfo().clusterName();
  }

  if (config.has_tracing()) {
    const auto& tracing_config = config.tracing();

    Tracing::OperationName tracing_operation_name;
    std::vector<Http::LowerCaseString> request_headers_for_tags;

    // Listener level traffic direction overrides the operation name
    switch (context.direction()) {
    case envoy::api::v2::core::TrafficDirection::UNSPECIFIED: {
      switch (tracing_config.operation_name()) {
      case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
          Tracing::INGRESS:
        tracing_operation_name = Tracing::OperationName::Ingress;
        break;
      case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
          Tracing::EGRESS:
        tracing_operation_name = Tracing::OperationName::Egress;
        break;
      default:
        NOT_REACHED_GCOVR_EXCL_LINE;
      }
      break;
    }
    case envoy::api::v2::core::TrafficDirection::INBOUND:
      tracing_operation_name = Tracing::OperationName::Ingress;
      break;
    case envoy::api::v2::core::TrafficDirection::OUTBOUND:
      tracing_operation_name = Tracing::OperationName::Egress;
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }

    for (const std::string& header : tracing_config.request_headers_for_tags()) {
      request_headers_for_tags.push_back(Http::LowerCaseString(header));
    }

    envoy::type::FractionalPercent client_sampling;
    client_sampling.set_numerator(
        tracing_config.has_client_sampling() ? tracing_config.client_sampling().value() : 100);
    envoy::type::FractionalPercent random_sampling;
    // TODO: Random sampling historically was an integer and default to out of 10,000. We should
    // deprecate that and move to a straight fractional percent config.
    uint64_t random_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
        tracing_config, random_sampling, 10000, 10000)};
    random_sampling.set_numerator(random_sampling_numerator);
    random_sampling.set_denominator(envoy::type::FractionalPercent::TEN_THOUSAND);
    envoy::type::FractionalPercent overall_sampling;
    overall_sampling.set_numerator(
        tracing_config.has_overall_sampling() ? tracing_config.overall_sampling().value() : 100);

    const uint32_t max_path_tag_length = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
        tracing_config, max_path_tag_length, Tracing::DefaultMaxPathTagLength);

    tracing_config_ =
        std::make_unique<Http::TracingConnectionManagerConfig>(Http::TracingConnectionManagerConfig{
            tracing_operation_name, request_headers_for_tags, client_sampling, random_sampling,
            overall_sampling, tracing_config.verbose(), max_path_tag_length});
  }

  for (const auto& access_log : config.access_log()) {
    AccessLog::InstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::fromProto(access_log, context_);
    access_logs_.push_back(current_access_log);
  }

  server_transformation_ = config.server_header_transformation();

  if (!config.server_name().empty()) {
    server_name_ = config.server_name();
  } else {
    server_name_ = Http::DefaultServerString::get();
  }

  switch (config.codec_type()) {
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::AUTO:
    codec_type_ = CodecType::AUTO;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::HTTP1:
    codec_type_ = CodecType::HTTP1;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::HTTP2:
    codec_type_ = CodecType::HTTP2;
    break;
  case envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::HTTP3:
    codec_type_ = CodecType::HTTP3;
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  const auto& filters = config.http_filters();
  for (int32_t i = 0; i < filters.size(); i++) {
    bool is_terminal = false;
    processFilter(filters[i], i, "http", filter_factories_, is_terminal);
    Config::Utility::validateTerminalFilters(filters[i].name(), "http", is_terminal,
                                             i == filters.size() - 1);
  }

  for (const auto& upgrade_config : config.upgrade_configs()) {
    const std::string& name = upgrade_config.upgrade_type();
    const bool enabled = upgrade_config.has_enabled() ? upgrade_config.enabled().value() : true;
    if (findUpgradeCaseInsensitive(upgrade_filter_factories_, name) !=
        upgrade_filter_factories_.end()) {
      throw EnvoyException(
          fmt::format("Error: multiple upgrade configs with the same name: '{}'", name));
    }
    if (!upgrade_config.filters().empty()) {
      std::unique_ptr<FilterFactoriesList> factories = std::make_unique<FilterFactoriesList>();
      for (int32_t j = 0; j < upgrade_config.filters().size(); j++) {
        bool is_terminal = false;
        processFilter(upgrade_config.filters(j), j, name, *factories, is_terminal);
        Config::Utility::validateTerminalFilters(upgrade_config.filters(j).name(), "http upgrade",
                                                 is_terminal,
                                                 j == upgrade_config.filters().size() - 1);
      }
      upgrade_filter_factories_.emplace(
          std::make_pair(name, FilterConfig{std::move(factories), enabled}));
    } else {
      std::unique_ptr<FilterFactoriesList> factories(nullptr);
      upgrade_filter_factories_.emplace(
          std::make_pair(name, FilterConfig{std::move(factories), enabled}));
    }
  }
}

void HttpConnectionManagerConfig::processFilter(
    const envoy::config::filter::network::http_connection_manager::v2::HttpFilter& proto_config,
    int i, absl::string_view prefix, std::list<Http::FilterFactoryCb>& filter_factories,
    bool& is_terminal) {
  const std::string& string_name = proto_config.name();

  ENVOY_LOG(debug, "    {} filter #{}", prefix, i);
  ENVOY_LOG(debug, "      name: {}", string_name);

  const Json::ObjectSharedPtr filter_config =
      MessageUtil::getJsonObjectFromMessage(proto_config.config());
  ENVOY_LOG(debug, "    config: {}", filter_config->asJsonString());

  // Now see if there is a factory that will accept the config.
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          string_name);
  Http::FilterFactoryCb callback;
  if (Config::Utility::allowDeprecatedV1Config(context_.runtime(), *filter_config)) {
    callback = factory.createFilterFactory(*filter_config->getObject("value", true), stats_prefix_,
                                           context_);
  } else {
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, context_.messageValidationVisitor(), factory);
    callback = factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);
  }
  is_terminal = factory.isTerminalFilter();
  filter_factories.push_back(callback);
}

Http::ServerConnectionPtr
HttpConnectionManagerConfig::createCodec(Network::Connection& connection,
                                         const Buffer::Instance& data,
                                         Http::ServerConnectionCallbacks& callbacks) {
  switch (codec_type_) {
  case CodecType::HTTP1:
    return std::make_unique<Http::Http1::ServerConnectionImpl>(
        connection, context_.scope(), callbacks, http1_settings_, maxRequestHeadersKb(),
        maxRequestHeadersCount());
  case CodecType::HTTP2:
    return std::make_unique<Http::Http2::ServerConnectionImpl>(
        connection, callbacks, context_.scope(), http2_settings_, maxRequestHeadersKb(),
        maxRequestHeadersCount());
  case CodecType::HTTP3:
    // TODO(danzh) create QUIC specific codec.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  case CodecType::AUTO:
    return Http::ConnectionManagerUtility::autoCreateCodec(
        connection, data, callbacks, context_.scope(), http1_settings_, http2_settings_,
        maxRequestHeadersKb(), maxRequestHeadersCount());
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

void HttpConnectionManagerConfig::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  for (const Http::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

bool HttpConnectionManagerConfig::createUpgradeFilterChain(
    absl::string_view upgrade_type,
    const Http::FilterChainFactory::UpgradeMap* per_route_upgrade_map,
    Http::FilterChainFactoryCallbacks& callbacks) {
  bool route_enabled = false;
  if (per_route_upgrade_map) {
    auto route_it = findUpgradeBoolCaseInsensitive(*per_route_upgrade_map, upgrade_type);
    if (route_it != per_route_upgrade_map->end()) {
      // Upgrades explicitly not allowed on this route.
      if (route_it->second == false) {
        return false;
      }
      // Upgrades explicitly enabled on this route.
      route_enabled = true;
    }
  }

  auto it = findUpgradeCaseInsensitive(upgrade_filter_factories_, upgrade_type);
  if ((it == upgrade_filter_factories_.end() || !it->second.allow_upgrade) && !route_enabled) {
    // Either the HCM disables upgrades and the route-config does not override,
    // or neither is configured for this upgrade.
    return false;
  }
  FilterFactoriesList* filters_to_use = &filter_factories_;
  if (it != upgrade_filter_factories_.end() && it->second.filter_factories != nullptr) {
    filters_to_use = it->second.filter_factories.get();
  }

  for (const Http::FilterFactoryCb& factory : *filters_to_use) {
    factory(callbacks);
  }
  return true;
}

const Network::Address::Instance& HttpConnectionManagerConfig::localAddress() {
  return *context_.localInfo().address();
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
