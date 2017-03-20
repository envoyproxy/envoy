#include "configuration_impl.h"

#include "envoy/network/connection.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/utility.h"
#include "common/json/config_schemas.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/ssl/context_config_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/upstream/cluster_manager_impl.h"

namespace Server {
namespace Configuration {

bool FilterChainUtility::buildFilterChain(Network::FilterManager& filter_manager,
                                          const std::list<NetworkFilterFactoryCb>& factories) {
  for (const NetworkFilterFactoryCb& factory : factories) {
    factory(filter_manager);
  }

  return filter_manager.initializeReadFilters();
}

MainImpl::MainImpl(Server::Instance& server) : server_(server) {}

void MainImpl::initialize(const Json::Object& json) {
  cluster_manager_factory_.reset(new Upstream::ProdClusterManagerFactory(
      server_.runtime(), server_.stats(), server_.threadLocal(), server_.random(),
      server_.dnsResolver(), server_.sslContextManager(), server_.dispatcher(),
      server_.localInfo()));
  cluster_manager_.reset(new Upstream::ClusterManagerImpl(
      *json.getObject("cluster_manager"), *cluster_manager_factory_, server_.stats(),
      server_.threadLocal(), server_.runtime(), server_.random(), server_.localInfo(),
      server_.accessLogManager()));

  std::vector<Json::ObjectPtr> listeners = json.getObjectArray("listeners");
  log().info("loading {} listener(s)", listeners.size());
  for (size_t i = 0; i < listeners.size(); i++) {
    log().info("listener #{}:", i);
    listeners_.emplace_back(
        Server::Configuration::ListenerPtr{new ListenerConfig(*this, *listeners[i])});
  }

  if (json.hasObject("statsd_local_udp_port")) {
    statsd_udp_port_.value(json.getInteger("statsd_local_udp_port"));
  }

  if (json.hasObject("statsd_tcp_cluster_name")) {
    statsd_tcp_cluster_name_.value(json.getString("statsd_tcp_cluster_name"));
  }

  stats_flush_interval_ =
      std::chrono::milliseconds(json.getInteger("stats_flush_interval_ms", 5000));

  if (json.hasObject("tracing")) {
    initializeTracers(*json.getObject("tracing"));
  } else {
    http_tracer_.reset(new Tracing::HttpNullTracer());
  }

  if (json.hasObject("rate_limit_service")) {
    Json::ObjectPtr rate_limit_service_config = json.getObject("rate_limit_service");
    std::string type = rate_limit_service_config->getString("type");
    if (type == "grpc_service") {
      ratelimit_client_factory_.reset(new RateLimit::GrpcFactoryImpl(
          *rate_limit_service_config->getObject("config"), *cluster_manager_));
    } else {
      throw EnvoyException(fmt::format("unknown rate limit service type '{}'", type));
    }
  } else {
    ratelimit_client_factory_.reset(new RateLimit::NullFactoryImpl());
  }
}

void MainImpl::initializeTracers(const Json::Object& tracing_configuration) {
  log().info("loading tracing configuration");

  // Initialize tracing driver.
  if (tracing_configuration.hasObject("http")) {
    Json::ObjectPtr http_tracer_config = tracing_configuration.getObject("http");
    Json::ObjectPtr driver = http_tracer_config->getObject("driver");

    std::string type = driver->getString("type");
    log().info(fmt::format("  loading tracing driver: {}", type));

    if (type == "lightstep") {
      ::Runtime::RandomGenerator& rand = server_.random();
      std::unique_ptr<lightstep::TracerOptions> opts(new lightstep::TracerOptions());
      opts->access_token = server_.api().fileReadToEnd(driver->getString("access_token_file"));
      StringUtil::rtrim(opts->access_token);

      if (server_.localInfo().clusterName().empty()) {
        throw EnvoyException("cluster name must be defined if LightStep tracing is enabled. See "
                             "--service-cluster option.");
      }
      opts->tracer_attributes["lightstep.component_name"] = server_.localInfo().clusterName();
      opts->guid_generator = [&rand]() { return rand.random(); };

      Tracing::DriverPtr lightstep_driver(new Tracing::LightStepDriver(
          *driver->getObject("config"), *cluster_manager_, server_.stats(), server_.threadLocal(),
          server_.runtime(), std::move(opts)));

      http_tracer_.reset(
          new Tracing::HttpTracerImpl(std::move(lightstep_driver), server_.localInfo()));
    } else {
      throw EnvoyException(fmt::format("unsupported driver type: '{}'", type));
    }
  }
}

const std::list<Server::Configuration::ListenerPtr>& MainImpl::listeners() { return listeners_; }

MainImpl::ListenerConfig::ListenerConfig(MainImpl& parent, Json::Object& json) : parent_(parent) {
  std::string addr = json.getString("address");
  address_ = Network::Utility::resolveUrl(addr);
  scope_ = parent_.server_.stats().createScope(fmt::format("listener.{}.", addr));
  log().info("  address={}", addr);

  json.validateSchema(Json::Schema::LISTENER_SCHEMA);

  if (json.hasObject("ssl_context")) {
    Ssl::ContextConfigImpl context_config(*json.getObject("ssl_context"));
    ssl_context_ =
        parent_.server_.sslContextManager().createSslServerContext(*scope_, context_config);
  }

  bind_to_port_ = json.getBoolean("bind_to_port", true);
  use_proxy_proto_ = json.getBoolean("use_proxy_proto", false);
  use_original_dst_ = json.getBoolean("use_original_dst", false);
  per_connection_buffer_limit_bytes_ =
      json.getInteger("per_connection_buffer_limit_bytes", 1024 * 1024);

  std::vector<Json::ObjectPtr> filters = json.getObjectArray("filters");
  for (size_t i = 0; i < filters.size(); i++) {
    std::string string_type = filters[i]->getString("type");
    std::string string_name = filters[i]->getString("name");
    Json::ObjectPtr config = filters[i]->getObject("config");
    log().info("  filter #{}:", i);
    log().info("    type: {}", string_type);
    log().info("    name: {}", string_name);

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
          config_factory->tryCreateFilterFactory(type, string_name, *config, parent_.server_);
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

bool MainImpl::ListenerConfig::createFilterChain(Network::Connection& connection) {
  return FilterChainUtility::buildFilterChain(connection, filter_factories_);
}

InitialImpl::InitialImpl(const Json::Object& json) {
  Json::ObjectPtr admin = json.getObject("admin");
  admin_.access_log_path_ = admin->getString("access_log_path");
  admin_.address_ = Network::Utility::resolveUrl(admin->getString("address"));

  if (json.hasObject("flags_path")) {
    flags_path_.value(json.getString("flags_path"));
  }

  if (json.hasObject("runtime")) {
    runtime_.reset(new RuntimeImpl());
    runtime_->symlink_root_ = json.getObject("runtime")->getString("symlink_root");
    runtime_->subdirectory_ = json.getObject("runtime")->getString("subdirectory");
    runtime_->override_subdirectory_ =
        json.getObject("runtime")->getString("override_subdirectory", "");
  }
}

} // Configuration
} // Server
