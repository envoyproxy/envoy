#include "extensions/filters/network/http_connection_manager/config.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/registry/registry.h"
#include "envoy/server/admin.h"
#include "envoy/stats/stats.h"

#include "common/access_log/access_log_impl.h"
#include "common/common/fmt.h"
#include "common/config/filter_json.h"
#include "common/config/utility.h"
#include "common/http/date_provider_impl.h"
#include "common/http/default_server_string.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/utility.h"
#include "common/router/rds_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(date_provider);
SINGLETON_MANAGER_REGISTRATION(route_config_provider_manager);

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

  std::shared_ptr<HttpConnectionManagerConfig> filter_config(new HttpConnectionManagerConfig(
      proto_config, context, *date_provider, *route_config_provider_manager));

  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count. Moreover, keep in mind the capture list determines
  // destruction order.
  return [route_config_provider_manager, filter_config, &context,
          date_provider](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
        *filter_config, context.drainDecision(), context.random(), context.httpTracer(),
        context.runtime(), context.localInfo(), context.clusterManager())});
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
static Registry::RegisterFactory<HttpConnectionManagerFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

std::string
HttpConnectionManagerConfigUtility::determineNextProtocol(Network::Connection& connection,
                                                          const Buffer::Instance& data) {
  if (!connection.nextProtocol().empty()) {
    return connection.nextProtocol();
  }

  // See if the data we have so far shows the HTTP/2 prefix. We ignore the case where someone sends
  // us the first few bytes of the HTTP/2 prefix since in all public cases we use SSL/ALPN. For
  // internal cases this should practically never happen.
  if (-1 != data.search(Http::Http2::CLIENT_MAGIC_PREFIX.c_str(),
                        Http::Http2::CLIENT_MAGIC_PREFIX.size(), 0)) {
    return Http::Http2::ALPN_STRING;
  }

  return "";
}

HttpConnectionManagerConfig::HttpConnectionManagerConfig(
    const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
        config,
    Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider,
    Router::RouteConfigProviderManager& route_config_provider_manager)
    : context_(context), stats_prefix_(fmt::format("http.{}.", config.stat_prefix())),
      stats_(Http::ConnectionManagerImpl::generateStats(stats_prefix_, context_.scope())),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats(stats_prefix_, context_.scope())),
      use_remote_address_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_remote_address, false)),
      xff_num_trusted_hops_(config.xff_num_trusted_hops()),
      skip_xff_append_(config.skip_xff_append()), via_(config.via()),
      route_config_provider_manager_(route_config_provider_manager),
      http2_settings_(Http::Utility::parseHttp2Settings(config.http2_protocol_options())),
      http1_settings_(Http::Utility::parseHttp1Settings(config.http_protocol_options())),
      drain_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(config, drain_timeout, 5000)),
      generate_request_id_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, generate_request_id, true)),
      date_provider_(date_provider),
      listener_stats_(Http::ConnectionManagerImpl::generateListenerStats(stats_prefix_,
                                                                         context_.listenerScope())),
      proxy_100_continue_(config.proxy_100_continue()) {

  route_config_provider_ = Router::RouteConfigProviderUtil::create(config, context_, stats_prefix_,
                                                                   route_config_provider_manager_);

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
    NOT_REACHED;
  }

  const auto& set_current_client_cert_details = config.set_current_client_cert_details();
  if (set_current_client_cert_details.cert()) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::Cert);
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(set_current_client_cert_details, subject, false)) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::Subject);
  }
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(set_current_client_cert_details, san, false)) {
    set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::SAN);
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
      NOT_REACHED;
    }

    for (const std::string& header : tracing_config.request_headers_for_tags()) {
      request_headers_for_tags.push_back(Http::LowerCaseString(header));
    }

    tracing_config_.reset(new Http::TracingConnectionManagerConfig(
        {tracing_operation_name, request_headers_for_tags}));
  }

  if (config.has_idle_timeout()) {
    idle_timeout_ = std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(config, idle_timeout));
  }

  for (const auto& access_log : config.access_log()) {
    AccessLog::InstanceSharedPtr current_access_log =
        AccessLog::AccessLogFactory::fromProto(access_log, context_);
    access_logs_.push_back(current_access_log);
  }

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
  default:
    NOT_REACHED;
  }

  const auto& filters = config.http_filters();
  for (int32_t i = 0; i < filters.size(); i++) {
    const ProtobufTypes::String& string_name = filters[i].name();
    const auto& proto_config = filters[i];

    ENVOY_LOG(debug, "    filter #{}", i);
    ENVOY_LOG(debug, "      name: {}", string_name);

    const Json::ObjectSharedPtr filter_config =
        MessageUtil::getJsonObjectFromMessage(proto_config.config());
    ENVOY_LOG(debug, "    config: {}", filter_config->asJsonString());

    // Now see if there is a factory that will accept the config.
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            string_name);
    Http::FilterFactoryCb callback;
    if (filter_config->getBoolean("deprecated_v1", false)) {
      callback = factory.createFilterFactory(*filter_config->getObject("value", true),
                                             stats_prefix_, context);
    } else {
      ProtobufTypes::MessagePtr message =
          Config::Utility::translateToFactoryConfig(proto_config, factory);
      callback = factory.createFilterFactoryFromProto(*message, stats_prefix_, context);
    }
    filter_factories_.push_back(callback);
  }
}

Http::ServerConnectionPtr
HttpConnectionManagerConfig::createCodec(Network::Connection& connection,
                                         const Buffer::Instance& data,
                                         Http::ServerConnectionCallbacks& callbacks) {
  switch (codec_type_) {
  case CodecType::HTTP1:
    return Http::ServerConnectionPtr{
        new Http::Http1::ServerConnectionImpl(connection, callbacks, http1_settings_)};
  case CodecType::HTTP2:
    return Http::ServerConnectionPtr{new Http::Http2::ServerConnectionImpl(
        connection, callbacks, context_.scope(), http2_settings_)};
  case CodecType::AUTO:
    if (HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data) ==
        Http::Http2::ALPN_STRING) {
      return Http::ServerConnectionPtr{new Http::Http2::ServerConnectionImpl(
          connection, callbacks, context_.scope(), http2_settings_)};
    } else {
      return Http::ServerConnectionPtr{
          new Http::Http1::ServerConnectionImpl(connection, callbacks, http1_settings_)};
    }
  }

  NOT_REACHED;
}

void HttpConnectionManagerConfig::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  for (const Http::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

const Network::Address::Instance& HttpConnectionManagerConfig::localAddress() {
  return *context_.localInfo().address();
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
