#include "http_connection_manager.h"

#include "envoy/filesystem/filesystem.h"
#include "envoy/network/connection.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"

#include "common/http/access_log/access_log_impl.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/http2/codec_impl.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"
#include "common/router/config_impl.h"
#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

const std::string HttpConnectionManagerConfig::DEFAULT_SERVER_STRING = "envoy";

/**
 * Config registration for the HTTP connection manager filter. @see NetworkFilterConfigFactory.
 */
class HttpConnectionManagerFilterConfigFactory : Logger::Loggable<Logger::Id::config>,
                                                 public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& config,
                                                Server::Instance& server) {
    if (type != NetworkFilterType::Read || name != "http_connection_manager") {
      return nullptr;
    }

    std::shared_ptr<HttpConnectionManagerConfig> http_config(
        new HttpConnectionManagerConfig(config, server));
    return [http_config, &server](Network::FilterManager& filter_manager) mutable -> void {
      filter_manager.addReadFilter(Network::ReadFilterPtr{
          new Http::ConnectionManagerImpl(*http_config, server.drainManager(), server.random(),
                                          server.httpTracer(), server.runtime())});
    };
  }
};

/**
 * Static registration for the HTTP connection manager filter. @see
 * RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<HttpConnectionManagerFilterConfigFactory> registered_;

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
                                                         Server::Instance& server)
    : server_(server), stats_prefix_(fmt::format("http.{}.", config.getString("stat_prefix"))),
      stats_(Http::ConnectionManagerImpl::generateStats(stats_prefix_, server.stats())),
      codec_options_(Http::Utility::parseCodecOptions(config)),
      route_config_(new Router::ConfigImpl(config.getObject("route_config"), server.runtime(),
                                           server.clusterManager())),
      drain_timeout_(config.getInteger("drain_timeout_ms", 5000)),
      generate_request_id_(config.getBoolean("generate_request_id", true)),
      date_provider_(server.dispatcher(), server.threadLocal()) {

  if (config.hasObject("use_remote_address")) {
    use_remote_address_ = config.getBoolean("use_remote_address");
  }

  if (config.hasObject("add_user_agent") && config.getBoolean("add_user_agent")) {
    user_agent_.value(server.options().serviceClusterName());
  }

  if (config.hasObject("tracing")) {
    const std::string operation_name = config.getObject("tracing").getString("operation_name");

    std::string tracing_type = config.getObject("tracing").getString("type", "all");
    Http::TracingType type;
    if (tracing_type == "all") {
      type = Http::TracingType::All;
    } else if (tracing_type == "upstream_failure") {
      type = Http::TracingType::UpstreamFailure;
    } else {
      throw EnvoyException(fmt::format("unsupported tracing type '{}'", tracing_type));
    }

    tracing_config_.value({operation_name, type});
  }

  if (config.hasObject("idle_timeout_s")) {
    idle_timeout_.value(std::chrono::seconds(config.getInteger("idle_timeout_s")));
  }

  if (config.hasObject("access_log")) {
    for (Json::Object& access_log : config.getObjectArray("access_log")) {
      Http::AccessLog::InstancePtr current_access_log = Http::AccessLog::InstanceImpl::fromJson(
          access_log, server.api(), server.dispatcher(), server.accessLogLock(), server.stats(),
          server.runtime());
      server.accessLogManager().registerAccessLog(current_access_log);
      access_logs_.push_back(current_access_log);
    }
  }

  server_name_ = config.getString("server_name", DEFAULT_SERVER_STRING);

  std::string codec_type = config.getString("codec_type");
  if (codec_type == "http1") {
    codec_type_ = CodecType::HTTP1;
  } else if (codec_type == "http2") {
    codec_type_ = CodecType::HTTP2;
  } else if (codec_type == "auto") {
    codec_type_ = CodecType::AUTO;
  } else {
    throw EnvoyException(fmt::format("invalid connection manager codec '{}'", codec_type));
  }

  std::vector<Json::Object> filters = config.getObjectArray("filters");
  for (size_t i = 0; i < filters.size(); i++) {
    std::string string_type = filters[i].getString("type");
    std::string string_name = filters[i].getString("name");
    Json::Object config = filters[i].getObject("config");

    log().info("    filter #{}", i);
    log().info("      type: {}", string_type);
    log().info("      name: {}", string_name);

    HttpFilterType type = stringToType(string_type);
    bool found_filter = false;
    for (HttpFilterConfigFactory* factory : filterConfigFactories()) {
      HttpFilterFactoryCb callback =
          factory->tryCreateFilterFactory(type, string_name, config, stats_prefix_, server);
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

Http::ServerConnectionPtr
HttpConnectionManagerConfig::createCodec(Network::Connection& connection,
                                         const Buffer::Instance& data,
                                         Http::ServerConnectionCallbacks& callbacks) {
  switch (codec_type_) {
  case CodecType::HTTP1:
    return Http::ServerConnectionPtr{new Http::Http1::ServerConnectionImpl(connection, callbacks)};
  case CodecType::HTTP2:
    return Http::ServerConnectionPtr{new Http::Http2::ServerConnectionImpl(
        connection, callbacks, server_.stats(), codec_options_)};
  case CodecType::AUTO:
    if (HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data) ==
        Http::Http2::ALPN_STRING) {
      return Http::ServerConnectionPtr{new Http::Http2::ServerConnectionImpl(
          connection, callbacks, server_.stats(), codec_options_)};
    } else {
      return Http::ServerConnectionPtr{
          new Http::Http1::ServerConnectionImpl(connection, callbacks)};
    }
  }

  NOT_IMPLEMENTED;
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
  } else if (type == "both") {
    return HttpFilterType::Both;
  } else {
    throw EnvoyException(fmt::format("invalid http filter type '{}'", type));
  }
}

const std::string& HttpConnectionManagerConfig::localAddress() { return server_.getLocalAddress(); }

} // Configuration
} // Server
