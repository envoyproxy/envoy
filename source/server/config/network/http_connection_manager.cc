#include "server/config/network/http_connection_manager.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/filesystem/filesystem.h"
#include "envoy/network/connection.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"

#include "common/http/access_log/access_log_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"
#include "common/json/config_schemas.h"
#include "common/router/rds_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {
namespace Configuration {

const std::string HttpConnectionManagerConfig::DEFAULT_SERVER_STRING = "envoy";

NetworkFilterFactoryCb
HttpConnectionManagerFilterConfigFactory::createFilterFactory(const Json::Object& config,
                                                              FactoryContext& context) {
  std::shared_ptr<HttpConnectionManagerConfig> http_config(
      new HttpConnectionManagerConfig(config, context));
  return [http_config, &context](Network::FilterManager& filter_manager) mutable -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
        *http_config, context.drainManager(), context.random(), context.httpTracer(),
        context.runtime(), context.localInfo())});
  };
}

/**
 * Static registration for the HTTP connection manager filter. @see
 * RegisterNamedNetworkFilterConfigFactory.
 */
static RegisterNamedNetworkFilterConfigFactory<HttpConnectionManagerFilterConfigFactory>
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
  if (-1 !=
      data.search(Http::Http2::CLIENT_MAGIC_PREFIX.c_str(), Http::Http2::CLIENT_MAGIC_PREFIX.size(),
                  0)) {
    return Http::Http2::ALPN_STRING;
  }

  return "";
}

HttpConnectionManagerConfig::HttpConnectionManagerConfig(const Json::Object& config,
                                                         FactoryContext& context)
    : Json::Validator(config, Json::Schema::HTTP_CONN_NETWORK_FILTER_SCHEMA), context_(context),
      stats_prefix_(fmt::format("http.{}.", config.getString("stat_prefix"))),
      stats_(Http::ConnectionManagerImpl::generateStats(stats_prefix_, context_.scope())),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats(stats_prefix_, context_.scope())),
      http2_settings_(Http::Utility::parseHttp2Settings(config)),
      drain_timeout_(config.getInteger("drain_timeout_ms", 5000)),
      generate_request_id_(config.getBoolean("generate_request_id", true)),
      date_provider_(context_.dispatcher(), context_.threadLocal()) {

  route_config_provider_ = Router::RouteConfigProviderUtil::create(
      config, context_.runtime(), context_.clusterManager(), context_.dispatcher(),
      context_.random(), context_.localInfo(), context_.scope(), stats_prefix_,
      context_.threadLocal(), context_.initManager());

  if (config.hasObject("use_remote_address")) {
    use_remote_address_ = config.getBoolean("use_remote_address");
  }

  const std::string forward_client_cert = config.getString("forward_client_cert", "sanitize");
  if (forward_client_cert == "forward_only") {
    forward_client_cert_ = Http::ForwardClientCertType::ForwardOnly;
  } else if (forward_client_cert == "append_forward") {
    forward_client_cert_ = Http::ForwardClientCertType::AppendForward;
  } else if (forward_client_cert == "sanitize_set") {
      forward_client_cert_ = Http::ForwardClientCertType::SanitizeSet;
  } else if (forward_client_cert == "always_forward_only") {
    forward_client_cert_ = Http::ForwardClientCertType::AlwaysForwardOnly;
  } else {
    ASSERT(forward_client_cert == "sanitize");
    forward_client_cert_ = Http::ForwardClientCertType::Sanitize;
  }

  if (config.hasObject("set_current_client_cert_details")) {
    for (const std::string detail : config.getStringArray("set_current_client_cert_details")) {
      if (detail == "Subject") {
        set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::Subject);
      } else {
        ASSERT(detail == "SAN");
        set_current_client_cert_details_.push_back(Http::ClientCertDetailsType::SAN);
      }
    }
  }

  if (config.hasObject("add_user_agent") && config.getBoolean("add_user_agent")) {
    user_agent_.value(context_.localInfo().clusterName());
  }

  if (config.hasObject("tracing")) {
    Json::ObjectSharedPtr tracing_config = config.getObject("tracing");

    const std::string operation_name = tracing_config->getString("operation_name");
    Tracing::OperationName tracing_operation_name;
    std::vector<Http::LowerCaseString> request_headers_for_tags;

    if (operation_name == "ingress") {
      tracing_operation_name = Tracing::OperationName::Ingress;
    } else {
      ASSERT(operation_name == "egress");
      tracing_operation_name = Tracing::OperationName::Egress;
    }

    if (tracing_config->hasObject("request_headers_for_tags")) {
      for (const std::string& header : tracing_config->getStringArray("request_headers_for_tags")) {
        request_headers_for_tags.push_back(Http::LowerCaseString(header));
      }
    }

    tracing_config_.reset(new Http::TracingConnectionManagerConfig(
        {tracing_operation_name, request_headers_for_tags}));
  }

  if (config.hasObject("idle_timeout_s")) {
    idle_timeout_.value(std::chrono::seconds(config.getInteger("idle_timeout_s")));
  }

  if (config.hasObject("access_log")) {
    for (const Json::ObjectSharedPtr& access_log : config.getObjectArray("access_log")) {
      Http::AccessLog::InstanceSharedPtr current_access_log =
          Http::AccessLog::InstanceImpl::fromJson(*access_log, context_.runtime(),
                                                  context_.accessLogManager());
      access_logs_.push_back(current_access_log);
    }
  }

  server_name_ = config.getString("server_name", DEFAULT_SERVER_STRING);

  std::string codec_type = config.getString("codec_type");
  if (codec_type == "http1") {
    codec_type_ = CodecType::HTTP1;
  } else if (codec_type == "http2") {
    codec_type_ = CodecType::HTTP2;
  } else {
    ASSERT(codec_type == "auto");
    codec_type_ = CodecType::AUTO;
  }

  std::vector<Json::ObjectSharedPtr> filters = config.getObjectArray("filters");
  for (size_t i = 0; i < filters.size(); i++) {
    std::string string_type = filters[i]->getString("type");
    std::string string_name = filters[i]->getString("name");
    Json::ObjectSharedPtr config_object = filters[i]->getObject("config");

    ENVOY_LOG(info, "    filter #{}", i);
    ENVOY_LOG(info, "      type: {}", string_type);
    ENVOY_LOG(info, "      name: {}", string_name);

    HttpFilterType type = stringToType(string_type);

    // Now see if there is a factory that will accept the config.
    auto search_it = namedFilterConfigFactories().find(string_name);
    if (search_it != namedFilterConfigFactories().end() && search_it->second->type() == type) {
      HttpFilterFactoryCb callback =
          search_it->second->createFilterFactory(*config_object, stats_prefix_, context_);
      filter_factories_.push_back(callback);
    } else {
      // DEPRECATED
      // This name wasn't found in the named map, so search in the deprecated list registry.
      bool found_filter = false;
      for (HttpFilterConfigFactory* config_factory : filterConfigFactories()) {
        HttpFilterFactoryCb callback = config_factory->tryCreateFilterFactory(
            type, string_name, *config_object, stats_prefix_, context_.server());
        if (callback) {
          filter_factories_.push_back(callback);
          found_filter = true;
          break;
        }
      }

      if (!found_filter) {
        throw EnvoyException(fmt::format("unable to create http filter factory for '{}'/'{}'",
                                         string_name, string_type));
      }
    }
  }
}

Http::ServerConnectionPtr
HttpConnectionManagerConfig::createCodec(Network::Connection& connection,
                                         const Buffer::Instance& data,
                                         Http::ServerConnectionCallbacks& callbacks) {
  switch (codec_type_) {
  case CodecType::HTTP1:
    return Http::ServerConnectionPtr{new Http::Http1::ServerConnectionImpl(connection, callbacks)};
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
          new Http::Http1::ServerConnectionImpl(connection, callbacks)};
    }
  }

  NOT_REACHED;
}

void HttpConnectionManagerConfig::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  for (const HttpFilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

HttpFilterType HttpConnectionManagerConfig::stringToType(const std::string& type) {
  if (type == "decoder") {
    return HttpFilterType::Decoder;
  } else if (type == "encoder") {
    return HttpFilterType::Encoder;
  } else {
    ASSERT(type == "both");
    return HttpFilterType::Both;
  }
}

const Network::Address::Instance& HttpConnectionManagerConfig::localAddress() {
  return *context_.localInfo().address();
}

} // Configuration
} // Server
} // Envoy
