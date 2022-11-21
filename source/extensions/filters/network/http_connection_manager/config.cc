#include "source/extensions/filters/network/http_connection_manager/config.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.validate.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/extensions/request_id/uuid/v3/uuid.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/registry/registry.h"
#include "envoy/server/admin.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/http/conn_manager_config.h"
#include "source/common/http/conn_manager_utility.h"
#include "source/common/http/default_server_string.h"
#include "source/common/http/filter_chain_helper.h"
#include "source/common/http/http1/codec_impl.h"
#include "source/common/http/http1/settings.h"
#include "source/common/http/http2/codec_impl.h"
#include "source/common/http/request_id_extension_impl.h"
#include "source/common/http/utility.h"
#include "source/common/local_reply/local_reply.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/scoped_rds.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/tracing/custom_tag_impl.h"
#include "source/common/tracing/http_tracer_manager_impl.h"
#include "source/common/tracing/tracer_config_impl.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/codec_impl.h"
#endif

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
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config) {
  if (config.has_internal_address_config()) {
    return std::make_unique<InternalAddressConfig>(config.internal_address_config());
  }

  return std::make_unique<Http::DefaultInternalAddressConfig>();
}

envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
    PathWithEscapedSlashesAction
    getPathWithEscapedSlashesActionRuntimeOverride(Server::Configuration::FactoryContext& context) {
  // The default behavior is to leave escaped slashes unchanged.
  uint64_t runtime_override = context.runtime().snapshot().getInteger(
      "http_connection_manager.path_with_escaped_slashes_action", 0);
  switch (runtime_override) {
  default:
    // Also includes runtime override values of 0 and 1
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        KEEP_UNCHANGED;
  case 2:
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        REJECT_REQUEST;
  case 3:
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        UNESCAPE_AND_REDIRECT;
  case 4:
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        UNESCAPE_AND_FORWARD;
  }
}

envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
    PathWithEscapedSlashesAction
    getPathWithEscapedSlashesAction(const envoy::extensions::filters::network::
                                        http_connection_manager::v3::HttpConnectionManager& config,
                                    Server::Configuration::FactoryContext& context) {
  envoy::type::v3::FractionalPercent default_fraction;
  default_fraction.set_numerator(100);
  default_fraction.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  if (context.runtime().snapshot().featureEnabled(
          "http_connection_manager.path_with_escaped_slashes_action_enabled", default_fraction)) {
    return config.path_with_escaped_slashes_action() ==
                   envoy::extensions::filters::network::http_connection_manager::v3::
                       HttpConnectionManager::IMPLEMENTATION_SPECIFIC_DEFAULT
               ? getPathWithEscapedSlashesActionRuntimeOverride(context)
               : config.path_with_escaped_slashes_action();
  }

  // When action is disabled through runtime the behavior is to keep escaped slashes unchanged.
  return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      KEEP_UNCHANGED;
}

Http::HeaderValidatorFactoryPtr
createHeaderValidatorFactory([[maybe_unused]] const envoy::extensions::filters::network::
                                 http_connection_manager::v3::HttpConnectionManager& config,
                             [[maybe_unused]] Server::Configuration::FactoryContext& context) {

  Http::HeaderValidatorFactoryPtr header_validator_factory;
#ifdef ENVOY_ENABLE_UHV
  ::envoy::config::core::v3::TypedExtensionConfig legacy_header_validator_config;
  if (!config.has_typed_header_validation_config()) {
    // If header validator is not configured ensure that the defaults match Envoy's original
    // behavior.
    ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        uhv_config;
    // By default legacy config had path normalization and merge slashes disabled.
    uhv_config.mutable_uri_path_normalization_options()->set_skip_path_normalization(
        !config.has_normalize_path() || !config.normalize_path().value());
    uhv_config.mutable_uri_path_normalization_options()->set_skip_merging_slashes(
        !config.merge_slashes());
    uhv_config.mutable_uri_path_normalization_options()->set_path_with_escaped_slashes_action(
        static_cast<
            ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig::
                UriPathNormalizationOptions::PathWithEscapedSlashesAction>(
            config.path_with_escaped_slashes_action()));
    uhv_config.mutable_http1_protocol_options()->set_allow_chunked_length(
        config.http_protocol_options().allow_chunked_length());
    legacy_header_validator_config.set_name("default_envoy_uhv_from_legacy_settings");
    legacy_header_validator_config.mutable_typed_config()->PackFrom(uhv_config);
  }

  const ::envoy::config::core::v3::TypedExtensionConfig& header_validator_config =
      config.has_typed_header_validation_config() ? config.typed_header_validation_config()
                                                  : legacy_header_validator_config;

  auto* factory = Envoy::Config::Utility::getFactory<Http::HeaderValidatorFactoryConfig>(
      header_validator_config);
  if (!factory) {
    throw EnvoyException(
        fmt::format("Header validator extension not found: '{}'", header_validator_config.name()));
  }

  header_validator_factory =
      factory->createFromProto(header_validator_config.typed_config(), context);
  if (!header_validator_factory) {
    throw EnvoyException(fmt::format("Header validator extension could not be created: '{}'",
                                     header_validator_config.name()));
  }
#else
  if (config.has_typed_header_validation_config()) {
    throw EnvoyException(
        fmt::format("This Envoy binary does not support header validator extensions.: '{}'",
                    config.typed_header_validation_config().name()));
  }
#endif
  return header_validator_factory;
}

} // namespace

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(date_provider);
SINGLETON_MANAGER_REGISTRATION(route_config_provider_manager);
SINGLETON_MANAGER_REGISTRATION(scoped_routes_config_provider_manager);
SINGLETON_MANAGER_REGISTRATION(http_tracer_manager);

Utility::Singletons Utility::createSingletons(Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Http::TlsCachingDateProviderImpl> date_provider =
      context.singletonManager().getTyped<Http::TlsCachingDateProviderImpl>(
          SINGLETON_MANAGER_REGISTERED_NAME(date_provider), [&context] {
            return std::make_shared<Http::TlsCachingDateProviderImpl>(
                context.mainThreadDispatcher(), context.threadLocal());
          });

  Router::RouteConfigProviderManagerSharedPtr route_config_provider_manager =
      context.singletonManager().getTyped<Router::RouteConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(route_config_provider_manager), [&context] {
            return std::make_shared<Router::RouteConfigProviderManagerImpl>(context.admin());
          });

  Router::ScopedRoutesConfigProviderManagerSharedPtr scoped_routes_config_provider_manager =
      context.singletonManager().getTyped<Router::ScopedRoutesConfigProviderManager>(
          SINGLETON_MANAGER_REGISTERED_NAME(scoped_routes_config_provider_manager),
          [&context, route_config_provider_manager] {
            return std::make_shared<Router::ScopedRoutesConfigProviderManager>(
                context.admin(), *route_config_provider_manager);
          });

  auto http_tracer_manager = context.singletonManager().getTyped<Tracing::HttpTracerManagerImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(http_tracer_manager), [&context] {
        return std::make_shared<Tracing::HttpTracerManagerImpl>(
            std::make_unique<Tracing::TracerFactoryContextImpl>(
                context.getServerFactoryContext(), context.messageValidationVisitor()));
      });

  std::shared_ptr<Http::DownstreamFilterConfigProviderManager> filter_config_provider_manager =
      Http::FilterChainUtility::createSingletonDownstreamFilterConfigProviderManager(
          context.getServerFactoryContext());

  return {date_provider, route_config_provider_manager, scoped_routes_config_provider_manager,
          http_tracer_manager, filter_config_provider_manager};
}

std::shared_ptr<HttpConnectionManagerConfig> Utility::createConfig(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
    Router::RouteConfigProviderManager& route_config_provider_manager,
    Config::ConfigProviderManager& scoped_routes_config_provider_manager,
    Tracing::HttpTracerManager& http_tracer_manager,
    FilterConfigProviderManager& filter_config_provider_manager) {
  return std::make_shared<HttpConnectionManagerConfig>(
      proto_config, context, date_provider, route_config_provider_manager,
      scoped_routes_config_provider_manager, http_tracer_manager, filter_config_provider_manager);
}

Network::FilterFactoryCb
HttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context) {
  return createFilterFactoryFromProtoAndHopByHop(proto_config, context, true);
}

Network::FilterFactoryCb
HttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoAndHopByHop(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context, bool clear_hop_by_hop_headers) {
  Utility::Singletons singletons = Utility::createSingletons(context);

  auto filter_config = Utility::createConfig(
      proto_config, context, *singletons.date_provider_, *singletons.route_config_provider_manager_,
      *singletons.scoped_routes_config_provider_manager_, *singletons.http_tracer_manager_,
      *singletons.filter_config_provider_manager_);

  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count.
  // Keep in mind the lambda capture list **doesn't** determine the destruction order, but it's fine
  // as these captured objects are also global singletons.
  return [singletons, filter_config, &context,
          clear_hop_by_hop_headers](Network::FilterManager& filter_manager) -> void {
    auto hcm = std::make_shared<Http::ConnectionManagerImpl>(
        *filter_config, context.drainDecision(), context.api().randomGenerator(),
        context.httpContext(), context.runtime(), context.localInfo(), context.clusterManager(),
        context.overloadManager(), context.mainThreadDispatcher().timeSource());
    if (!clear_hop_by_hop_headers) {
      hcm->setClearHopByHopResponseHeaders(false);
    }
    filter_manager.addReadFilter(std::move(hcm));
  };
}

Network::FilterFactoryCb
MobileHttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::http_connection_manager::v3::
        EnvoyMobileHttpConnectionManager& mobile_config,
    Server::Configuration::FactoryContext& context) {
  return HttpConnectionManagerFilterConfigFactory::createFilterFactoryFromProtoAndHopByHop(
      mobile_config.config(), context, false);
}

/**
 * Static registration for the HTTP connection manager filter.
 */
REGISTER_FACTORY(HttpConnectionManagerFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){
    "envoy.http_connection_manager"};

InternalAddressConfig::InternalAddressConfig(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        InternalAddressConfig& config)
    : unix_sockets_(config.unix_sockets()), cidr_ranges_(config.cidr_ranges()) {}

HttpConnectionManagerConfig::HttpConnectionManagerConfig(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config,
    Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
    Router::RouteConfigProviderManager& route_config_provider_manager,
    Config::ConfigProviderManager& scoped_routes_config_provider_manager,
    Tracing::HttpTracerManager& http_tracer_manager,
    FilterConfigProviderManager& filter_config_provider_manager)
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
      filter_config_provider_manager_(filter_config_provider_manager),
      http3_options_(Http3::Utility::initializeAndValidateOptions(
          config.http3_protocol_options(), config.has_stream_error_on_invalid_http_message(),
          config.stream_error_on_invalid_http_message())),
      http2_options_(Http2::Utility::initializeAndValidateOptions(
          config.http2_protocol_options(), config.has_stream_error_on_invalid_http_message(),
          config.stream_error_on_invalid_http_message())),
      http1_settings_(Http::Http1::parseHttp1Settings(
          config.http_protocol_options(), context.messageValidationVisitor(),
          config.stream_error_on_invalid_http_message(),
          xff_num_trusted_hops_ == 0 && use_remote_address_)),
      max_request_headers_kb_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config, max_request_headers_kb, Http::DEFAULT_MAX_REQUEST_HEADERS_KB)),
      max_request_headers_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.common_http_protocol_options(), max_headers_count,
          context.runtime().snapshot().getInteger(Http::MaxRequestHeadersCountOverrideKey,
                                                  Http::DEFAULT_MAX_HEADERS_COUNT))),
      idle_timeout_(PROTOBUF_GET_OPTIONAL_MS(config.common_http_protocol_options(), idle_timeout)),
      max_connection_duration_(
          PROTOBUF_GET_OPTIONAL_MS(config.common_http_protocol_options(), max_connection_duration)),
      max_stream_duration_(
          PROTOBUF_GET_OPTIONAL_MS(config.common_http_protocol_options(), max_stream_duration)),
      stream_idle_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, stream_idle_timeout, StreamIdleTimeoutMs)),
      request_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, request_timeout, RequestTimeoutMs)),
      request_headers_timeout_(
          PROTOBUF_GET_MS_OR_DEFAULT(config, request_headers_timeout, RequestHeaderTimeoutMs)),
      drain_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, drain_timeout, 5000)),
      generate_request_id_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, generate_request_id, true)),
      preserve_external_request_id_(config.preserve_external_request_id()),
      always_set_request_id_in_response_(config.always_set_request_id_in_response()),
      date_provider_(date_provider),
      listener_stats_(Http::ConnectionManagerImpl::generateListenerStats(stats_prefix_,
                                                                         context_.listenerScope())),
      proxy_100_continue_(config.proxy_100_continue()),
      stream_error_on_invalid_http_messaging_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, stream_error_on_invalid_http_message, false)),
      delayed_close_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, delayed_close_timeout, 1000)),
#ifdef ENVOY_NORMALIZE_PATH_BY_DEFAULT
      normalize_path_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config, normalize_path,
          // TODO(htuch): we should have a boolean variant of featureEnabled() here.
          context.runtime().snapshot().featureEnabled("http_connection_manager.normalize_path",
                                                      100))),
#else
      normalize_path_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config, normalize_path,
          // TODO(htuch): we should have a boolean variant of featureEnabled() here.
          context.runtime().snapshot().featureEnabled("http_connection_manager.normalize_path",
                                                      0))),
#endif
      merge_slashes_(config.merge_slashes()),
      headers_with_underscores_action_(
          config.common_http_protocol_options().headers_with_underscores_action()),
      local_reply_(LocalReply::Factory::create(config.local_reply_config(), context)),
      path_with_escaped_slashes_action_(getPathWithEscapedSlashesAction(config, context)),
      strip_trailing_host_dot_(config.strip_trailing_host_dot()),
      max_requests_per_connection_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
          config.common_http_protocol_options(), max_requests_per_connection, 0)),
      proxy_status_config_(config.has_proxy_status_config()
                               ? std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>(
                                     config.proxy_status_config())
                               : nullptr),
      header_validator_factory_(createHeaderValidatorFactory(config, context)),
      append_x_forwarded_port_(config.append_x_forwarded_port()) {
  if (!idle_timeout_) {
    idle_timeout_ = std::chrono::hours(1);
  } else if (idle_timeout_.value().count() == 0) {
    idle_timeout_ = absl::nullopt;
  }

  if (config.strip_any_host_port() && config.strip_matching_host_port()) {
    throw EnvoyException(fmt::format(
        "Error: Only one of `strip_matching_host_port` or `strip_any_host_port` can be set."));
  }

  if (config.strip_any_host_port()) {
    strip_port_type_ = Http::StripPortType::Any;
  } else if (config.strip_matching_host_port()) {
    strip_port_type_ = Http::StripPortType::MatchingHost;
  } else {
    strip_port_type_ = Http::StripPortType::None;
  }

  // If we are provided a different request_id_extension implementation to use try and create a
  // new instance of it, otherwise use default one.
  envoy::extensions::filters::network::http_connection_manager::v3::RequestIDExtension
      final_rid_config = config.request_id_extension();
  if (!final_rid_config.has_typed_config()) {
    // This creates a default version of the UUID extension which is a required extension in the
    // build.
    final_rid_config.mutable_typed_config()->PackFrom(
        envoy::extensions::request_id::uuid::v3::UuidRequestIdConfig());
  }
  request_id_extension_ = Http::RequestIDExtensionFactory::fromProto(final_rid_config, context_);

  // Check if IP detection extensions were configured, otherwise fall back to XFF.
  auto ip_detection_extensions = config.original_ip_detection_extensions();
  if (ip_detection_extensions.empty()) {
    envoy::extensions::http::original_ip_detection::xff::v3::XffConfig xff_config;
    xff_config.set_xff_num_trusted_hops(xff_num_trusted_hops_);

    auto* extension = ip_detection_extensions.Add();
    extension->set_name("envoy.http.original_ip_detection.xff");
    extension->mutable_typed_config()->PackFrom(xff_config);
  } else {
    if (use_remote_address_) {
      throw EnvoyException(
          "Original IP detection extensions and use_remote_address may not be mixed");
    }

    if (xff_num_trusted_hops_ > 0) {
      throw EnvoyException(
          "Original IP detection extensions and xff_num_trusted_hops may not be mixed");
    }
  }

  original_ip_detection_extensions_.reserve(ip_detection_extensions.size());
  for (const auto& extension_config : ip_detection_extensions) {
    auto* factory =
        Envoy::Config::Utility::getFactory<Http::OriginalIPDetectionFactory>(extension_config);
    if (!factory) {
      throw EnvoyException(
          fmt::format("Original IP detection extension not found: '{}'", extension_config.name()));
    }

    auto extension = factory->createExtension(extension_config.typed_config(), context_);
    if (!extension) {
      throw EnvoyException(fmt::format("Original IP detection extension could not be created: '{}'",
                                       extension_config.name()));
    }
    original_ip_detection_extensions_.push_back(extension);
  }

  // If scoped RDS is enabled, avoid creating a route config provider. Route config providers will
  // be managed by the scoped routing logic instead.
  switch (config.route_specifier_case()) {
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kRds:
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kRouteConfig:
    route_config_provider_ = Router::RouteConfigProviderUtil::create(
        config, context_.getServerFactoryContext(), context_.messageValidationVisitor(),
        context_.initManager(), stats_prefix_, route_config_provider_manager_);
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::kScopedRoutes:
    scoped_routes_config_provider_ = Router::ScopedRoutesConfigProviderUtil::create(
        config, context_.getServerFactoryContext(), context_.initManager(), stats_prefix_,
        scoped_routes_config_provider_manager_);
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      RouteSpecifierCase::ROUTE_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  switch (config.forward_client_cert_details()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE:
    forward_client_cert_ = Http::ForwardClientCertType::Sanitize;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      FORWARD_ONLY:
    forward_client_cert_ = Http::ForwardClientCertType::ForwardOnly;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      APPEND_FORWARD:
    forward_client_cert_ = Http::ForwardClientCertType::AppendForward;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SANITIZE_SET:
    forward_client_cert_ = Http::ForwardClientCertType::SanitizeSet;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ALWAYS_FORWARD_ONLY:
    forward_client_cert_ = Http::ForwardClientCertType::AlwaysForwardOnly;
    break;
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
    http_tracer_ = http_tracer_manager.getOrCreateHttpTracer(getPerFilterTracerConfig(config));

    const auto& tracing_config = config.tracing();

    Tracing::OperationName tracing_operation_name;

    // Listener level traffic direction overrides the operation name
    switch (context.direction()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::core::v3::UNSPECIFIED:
      // Continuing legacy behavior; if unspecified, we treat this as ingress.
      tracing_operation_name = Tracing::OperationName::Ingress;
      break;
    case envoy::config::core::v3::INBOUND:
      tracing_operation_name = Tracing::OperationName::Ingress;
      break;
    case envoy::config::core::v3::OUTBOUND:
      tracing_operation_name = Tracing::OperationName::Egress;
      break;
    }

    Tracing::CustomTagMap custom_tags;
    for (const auto& tag : tracing_config.custom_tags()) {
      custom_tags.emplace(tag.tag(), Tracing::CustomTagUtility::createCustomTag(tag));
    }

    envoy::type::v3::FractionalPercent client_sampling;
    client_sampling.set_numerator(
        tracing_config.has_client_sampling() ? tracing_config.client_sampling().value() : 100);
    envoy::type::v3::FractionalPercent random_sampling;
    // TODO: Random sampling historically was an integer and default to out of 10,000. We should
    // deprecate that and move to a straight fractional percent config.
    uint64_t random_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
        tracing_config, random_sampling, 10000, 10000)};
    random_sampling.set_numerator(random_sampling_numerator);
    random_sampling.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
    envoy::type::v3::FractionalPercent overall_sampling;
    uint64_t overall_sampling_numerator{PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
        tracing_config, overall_sampling, 10000, 10000)};
    overall_sampling.set_numerator(overall_sampling_numerator);
    overall_sampling.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

    const uint32_t max_path_tag_length = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
        tracing_config, max_path_tag_length, Tracing::DefaultMaxPathTagLength);

    tracing_config_ =
        std::make_unique<Http::TracingConnectionManagerConfig>(Http::TracingConnectionManagerConfig{
            tracing_operation_name, custom_tags, client_sampling, random_sampling, overall_sampling,
            tracing_config.verbose(), max_path_tag_length});
  }

  for (const auto& access_log : config.access_log()) {
    AccessLog::InstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::fromProto(access_log, context_);
    access_logs_.push_back(current_access_log);
  }

  server_transformation_ = config.server_header_transformation();

  if (!config.scheme_header_transformation().scheme_to_overwrite().empty()) {
    scheme_to_set_ = config.scheme_header_transformation().scheme_to_overwrite();
  }

  if (!config.server_name().empty()) {
    server_name_ = config.server_name();
  } else {
    server_name_ = Http::DefaultServerString::get();
  }

  switch (config.codec_type()) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      AUTO:
    codec_type_ = CodecType::AUTO;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      HTTP1:
    codec_type_ = CodecType::HTTP1;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      HTTP2:
    codec_type_ = CodecType::HTTP2;
    break;
  case envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      HTTP3:
#ifdef ENVOY_ENABLE_QUIC
    codec_type_ = CodecType::HTTP3;
    if (!context_.isQuicListener()) {
      throw EnvoyException("HTTP/3 codec configured on non-QUIC listener.");
    }
#else
    throw EnvoyException("HTTP3 configured but not enabled in the build.");
#endif
    break;
  }
  if (codec_type_ != CodecType::HTTP3 && context_.isQuicListener()) {
    throw EnvoyException("Non-HTTP/3 codec configured on QUIC listener.");
  }

  Http::FilterChainHelper<Server::Configuration::FactoryContext,
                          Server::Configuration::NamedHttpFilterConfigFactory>
      helper(filter_config_provider_manager_, context_.getServerFactoryContext(), context_,
             stats_prefix_);
  helper.processFilters(config.http_filters(), "http", "http", filter_factories_);

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
      Http::DependencyManager upgrade_dependency_manager;
      helper.processFilters(upgrade_config.filters(), name, "http upgrade", *factories);
      // TODO(auni53): Validate encode dependencies too.
      auto status = upgrade_dependency_manager.validDecodeDependencies();
      if (!status.ok()) {
        throw EnvoyException(std::string(status.message()));
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

Http::ServerConnectionPtr
HttpConnectionManagerConfig::createCodec(Network::Connection& connection,
                                         const Buffer::Instance& data,
                                         Http::ServerConnectionCallbacks& callbacks) {
  switch (codec_type_) {
  case CodecType::HTTP1:
    return std::make_unique<Http::Http1::ServerConnectionImpl>(
        connection, Http::Http1::CodecStats::atomicGet(http1_codec_stats_, context_.scope()),
        callbacks, http1_settings_, maxRequestHeadersKb(), maxRequestHeadersCount(),
        headersWithUnderscoresAction());
  case CodecType::HTTP2:
    return std::make_unique<Http::Http2::ServerConnectionImpl>(
        connection, callbacks,
        Http::Http2::CodecStats::atomicGet(http2_codec_stats_, context_.scope()),
        context_.api().randomGenerator(), http2_options_, maxRequestHeadersKb(),
        maxRequestHeadersCount(), headersWithUnderscoresAction());
  case CodecType::HTTP3:
#ifdef ENVOY_ENABLE_QUIC
    return std::make_unique<Quic::QuicHttpServerConnectionImpl>(
        dynamic_cast<Quic::EnvoyQuicServerSession&>(connection), callbacks,
        Http::Http3::CodecStats::atomicGet(http3_codec_stats_, context_.scope()), http3_options_,
        maxRequestHeadersKb(), maxRequestHeadersCount(), headersWithUnderscoresAction());
#else
    // Should be blocked by configuration checking at an earlier point.
    PANIC("unexpected");
#endif
  case CodecType::AUTO:
    return Http::ConnectionManagerUtility::autoCreateCodec(
        connection, data, callbacks, context_.scope(), context_.api().randomGenerator(),
        http1_codec_stats_, http2_codec_stats_, http1_settings_, http2_options_,
        maxRequestHeadersKb(), maxRequestHeadersCount(), headersWithUnderscoresAction());
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool HttpConnectionManagerConfig::createFilterChain(Http::FilterChainManager& manager, bool) const {
  Http::FilterChainUtility::createFilterChainForFactories(manager, filter_factories_);
  return true;
}

bool HttpConnectionManagerConfig::createUpgradeFilterChain(
    absl::string_view upgrade_type,
    const Http::FilterChainFactory::UpgradeMap* per_route_upgrade_map,
    Http::FilterChainManager& callbacks) const {
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
  const FilterFactoriesList* filters_to_use = &filter_factories_;
  if (it != upgrade_filter_factories_.end() && it->second.filter_factories != nullptr) {
    filters_to_use = it->second.filter_factories.get();
  }

  Http::FilterChainUtility::createFilterChainForFactories(callbacks, *filters_to_use);
  return true;
}

const Network::Address::Instance& HttpConnectionManagerConfig::localAddress() {
  return *context_.localInfo().address();
}

/**
 * Determines what tracing provider to use for a given
 * "envoy.filters.network.http_connection_manager" filter instance.
 */
const envoy::config::trace::v3::Tracing_Http* HttpConnectionManagerConfig::getPerFilterTracerConfig(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        config) {
  // Give precedence to tracing provider configuration defined as part of
  // "envoy.filters.network.http_connection_manager" filter config.
  if (config.tracing().has_provider()) {
    return &config.tracing().provider();
  }
  // Otherwise, for the sake of backwards compatibility, fall back to using tracing provider
  // configuration defined in the bootstrap config.
  if (context_.httpContext().defaultTracingConfig().has_http()) {
    return &context_.httpContext().defaultTracingConfig().http();
  }
  return nullptr;
}

std::function<Http::ApiListenerPtr()>
HttpConnectionManagerFactory::createHttpConnectionManagerFactoryFromProto(
    const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
        proto_config,
    Server::Configuration::FactoryContext& context, Network::ReadFilterCallbacks& read_callbacks,
    bool clear_hop_by_hop_headers) {

  Utility::Singletons singletons = Utility::createSingletons(context);

  auto filter_config = Utility::createConfig(
      proto_config, context, *singletons.date_provider_, *singletons.route_config_provider_manager_,
      *singletons.scoped_routes_config_provider_manager_, *singletons.http_tracer_manager_,
      *singletons.filter_config_provider_manager_);

  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count.
  // Keep in mind the lambda capture list **doesn't** determine the destruction order, but it's fine
  // as these captured objects are also global singletons.
  return [singletons, filter_config, &context, &read_callbacks,
          clear_hop_by_hop_headers]() -> Http::ApiListenerPtr {
    auto conn_manager = std::make_unique<Http::ConnectionManagerImpl>(
        *filter_config, context.drainDecision(), context.api().randomGenerator(),
        context.httpContext(), context.runtime(), context.localInfo(), context.clusterManager(),
        context.overloadManager(), context.mainThreadDispatcher().timeSource());
    if (!clear_hop_by_hop_headers) {
      conn_manager->setClearHopByHopResponseHeaders(false);
    }

    // This factory creates a new ConnectionManagerImpl in the absence of its usual environment as
    // an L4 filter, so this factory needs to take a few actions.

    // When a new connection is creating its filter chain it hydrates the factory with a filter
    // manager which provides the ConnectionManager with its "read_callbacks".
    conn_manager->initializeReadFilterCallbacks(read_callbacks);

    // When the connection first calls onData on the ConnectionManager, the ConnectionManager
    // creates a codec. Here we force create a codec as onData will not be called.
    Buffer::OwnedImpl dummy;
    conn_manager->createCodec(dummy);

    return conn_manager;
  };
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
