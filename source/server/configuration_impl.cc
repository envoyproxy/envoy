#include "configuration_impl.h"

#include "envoy/network/connection.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/utility.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/ssl/context_config_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/upstream/cluster_manager_impl.h"

namespace Server {
namespace Configuration {

MainImpl::MainImpl(Server::Instance& server) : server_(server) {}

void MainImpl::initialize(const std::string& file_path) {
  Json::FileLoader loader(file_path);

  cluster_manager_.reset(new Upstream::ProdClusterManagerImpl(
      loader.getObject("cluster_manager"), server_.stats(), server_.threadLocal(),
      server_.dnsResolver(), server_.sslContextManager(), server_.runtime(), server_.random(),
      server_.options().serviceZone(), server_.getLocalAddress()));

  std::vector<Json::Object> listeners = loader.getObjectArray("listeners");
  log().notice("loading {} listener(s)", listeners.size());
  for (size_t i = 0; i < listeners.size(); i++) {
    log().notice("listener #{}:", i);
    listeners_.emplace_back(
        Server::Configuration::ListenerPtr{new ListenerConfig(*this, listeners[i])});
  }

  if (loader.hasObject("statsd_local_udp_port")) {
    statsd_udp_port_.value(loader.getInteger("statsd_local_udp_port"));
  }

  if (loader.hasObject("statsd_tcp_cluster_name")) {
    statsd_tcp_cluster_name_.value(loader.getString("statsd_tcp_cluster_name"));
  }

  if (loader.hasObject("tracing")) {
    initializeTracers(loader.getObject("tracing"));
  } else {
    http_tracer_.reset(new Tracing::HttpNullTracer());
  }

  if (loader.hasObject("rate_limit_service")) {
    Json::Object rate_limit_service_config = loader.getObject("rate_limit_service");
    std::string type = rate_limit_service_config.getString("type");
    if (type == "grpc_service") {
      ratelimit_client_factory_.reset(new RateLimit::GrpcFactoryImpl(
          rate_limit_service_config.getObject("config"), *cluster_manager_, server_.stats()));
    } else {
      throw EnvoyException(fmt::format("unknown rate limit service type '{}'", type));
    }
  } else {
    ratelimit_client_factory_.reset(new RateLimit::NullFactoryImpl());
  }
}

void MainImpl::initializeTracers(const Json::Object& tracing_configuration_) {
  log().notice("loading tracing configuration");

  // Initialize http sinks
  if (tracing_configuration_.hasObject("http")) {
    http_tracer_.reset(new Tracing::HttpTracerImpl(server_.runtime(), server_.stats()));

    Json::Object http_tracer_config = tracing_configuration_.getObject("http");

    if (http_tracer_config.hasObject("sinks")) {
      std::vector<Json::Object> sinks = http_tracer_config.getObjectArray("sinks");
      log().notice(fmt::format("  loading {} http sink(s):", sinks.size()));

      for (const Json::Object& sink : sinks) {
        std::string type = sink.getString("type");
        log().notice(fmt::format("    loading {}", type));

        if (type == "lightstep") {
          ::Runtime::RandomGenerator& rand = server_.random();
          std::unique_ptr<lightstep::TracerOptions> opts(new lightstep::TracerOptions());
          opts->access_token = server_.api().fileReadToEnd(sink.getString("access_token_file"));
          StringUtil::rtrim(opts->access_token);

          opts->tracer_attributes["lightstep.guid"] = rand.uuid();
          opts->tracer_attributes["lightstep.component_name"] =
              server_.options().serviceClusterName();
          opts->guid_generator = [&rand]() { return rand.random(); };

          http_tracer_->addSink(Tracing::HttpSinkPtr{new Tracing::LightStepSink(
              sink.getObject("config"), *cluster_manager_, server_.stats(),
              server_.options().serviceNodeName(), server_.threadLocal(), server_.runtime(),
              std::move(opts))});
        } else {
          throw EnvoyException(fmt::format("Unsupported sink type: '{}'", type));
        }
      }
    }
  } else {
    throw EnvoyException("Incorrect tracing configuration");
  }
}

const std::list<Server::Configuration::ListenerPtr>& MainImpl::listeners() { return listeners_; }

MainImpl::ListenerConfig::ListenerConfig(MainImpl& parent, Json::Object& json)
    : parent_(parent), port_(json.getInteger("port")) {
  log().notice("  port={}", port_);

  if (json.hasObject("ssl_context")) {
    Ssl::ContextConfigImpl context_config(json.getObject("ssl_context"));
    ssl_context_ = &parent_.server_.sslContextManager().createSslServerContext(
        fmt::format("listener.{}", port_), parent_.server_.stats(), context_config);
  }

  if (json.hasObject("use_proxy_proto")) {
    use_proxy_proto_ = json.getBoolean("use_proxy_proto");
  }

  std::vector<Json::Object> filters = json.getObjectArray("filters");
  for (size_t i = 0; i < filters.size(); i++) {
    std::string string_type = filters[i].getString("type");
    std::string string_name = filters[i].getString("name");
    Json::Object config = filters[i].getObject("config");
    log().notice("  filter #{}:", i);
    log().notice("    type: {}", string_type);
    log().notice("    name: {}", string_name);

    // Map filter type string to enum.
    NetworkFilterType type;
    if (string_type == "read") {
      type = NetworkFilterType::Read;
    } else if (string_type == "write") {
      type = NetworkFilterType::Write;
    } else if (string_type == "both") {
      type = NetworkFilterType::Both;
    } else {
      throw EnvoyException(fmt::format("invalid filter type '{}'", string_type));
    }

    // Now see if there is a factory that will accept the config.
    bool found_filter = false;
    for (NetworkFilterConfigFactory* config_factory : filterConfigFactories()) {
      NetworkFilterFactoryCb callback =
          config_factory->tryCreateFilterFactory(type, string_name, config, parent_.server_);
      if (callback) {
        filter_factories_.push_back(callback);
        found_filter = true;
        break;
      }
    }

    if (!found_filter) {
      throw EnvoyException(
          fmt::format("unable to create filter factory for '{}'/'{}'", string_name, string_type));
    }
  }
}

void MainImpl::ListenerConfig::createFilterChain(Network::Connection& connection) {
  for (const NetworkFilterFactoryCb& factory : filter_factories_) {
    factory(connection);
  }
}

InitialImpl::InitialImpl(const std::string& file_path) {
  Json::FileLoader loader(file_path);
  Json::Object admin = loader.getObject("admin");
  admin_.access_log_path_ = admin.getString("access_log_path");
  admin_.port_ = admin.getInteger("port");

  if (loader.hasObject("flags_path")) {
    flags_path_.value(loader.getString("flags_path"));
  }

  if (loader.hasObject("runtime")) {
    runtime_.reset(new RuntimeImpl());
    runtime_->symlink_root_ = loader.getObject("runtime").getString("symlink_root");
    runtime_->subdirectory_ = loader.getObject("runtime").getString("subdirectory");
    runtime_->override_subdirectory_ =
        loader.getObject("runtime").getString("override_subdirectory", "");
  }
}

} // Configuration
} // Server
