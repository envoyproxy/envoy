#include "server/configuration_impl.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/connection.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/json/config_schemas.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/ssl/context_config_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/lightstep_tracer_impl.h"
#include "common/tracing/zipkin/zipkin_tracer_impl.h"
#include "common/upstream/cluster_manager_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {
namespace Configuration {

bool FilterChainUtility::buildFilterChain(Network::FilterManager& filter_manager,
                                          const std::list<NetworkFilterFactoryCb>& factories) {
  for (const NetworkFilterFactoryCb& factory : factories) {
    factory(filter_manager);
  }

  return filter_manager.initializeReadFilters();
}

MainImpl::MainImpl(Server::Instance& server,
                   Upstream::ClusterManagerFactory& cluster_manager_factory)
    : server_(server), cluster_manager_factory_(cluster_manager_factory) {}

void MainImpl::initialize(const Json::Object& json) {
  cluster_manager_ = cluster_manager_factory_.clusterManagerFromJson(
      *json.getObject("cluster_manager"), server_.stats(), server_.threadLocal(), server_.runtime(),
      server_.random(), server_.localInfo(), server_.accessLogManager());

  std::vector<Json::ObjectSharedPtr> listeners = json.getObjectArray("listeners");
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

  watchdog_miss_timeout_ =
      std::chrono::milliseconds(json.getInteger("watchdog_miss_timeout_ms", 200));
  watchdog_megamiss_timeout_ =
      std::chrono::milliseconds(json.getInteger("watchdog_megamiss_timeout_ms", 1000));
  watchdog_kill_timeout_ =
      std::chrono::milliseconds(json.getInteger("watchdog_kill_timeout_ms", 0));
  watchdog_multikill_timeout_ =
      std::chrono::milliseconds(json.getInteger("watchdog_multikill_timeout_ms", 0));

  initializeTracers(json);

  if (json.hasObject("rate_limit_service")) {
    Json::ObjectSharedPtr rate_limit_service_config = json.getObject("rate_limit_service");
    std::string type = rate_limit_service_config->getString("type");
    ASSERT(type == "grpc_service");
    UNREFERENCED_PARAMETER(type);
    ratelimit_client_factory_.reset(new RateLimit::GrpcFactoryImpl(
        *rate_limit_service_config->getObject("config"), *cluster_manager_));
  } else {
    ratelimit_client_factory_.reset(new RateLimit::NullFactoryImpl());
  }
}

void MainImpl::initializeTracers(const Json::Object& configuration) {
  log().info("loading tracing configuration");

  if (!configuration.hasObject("tracing")) {
    http_tracer_.reset(new Tracing::HttpNullTracer());
    return;
  }

  Json::ObjectSharedPtr tracing_configuration = configuration.getObject("tracing");
  if (!tracing_configuration->hasObject("http")) {
    http_tracer_.reset(new Tracing::HttpNullTracer());
    return;
  }

  if (server_.localInfo().clusterName().empty()) {
    throw EnvoyException("cluster name must be defined if tracing is enabled. See "
                         "--service-cluster option.");
  }

  // Initialize tracing driver.
  Json::ObjectSharedPtr http_tracer_config = tracing_configuration->getObject("http");
  Json::ObjectSharedPtr driver = http_tracer_config->getObject("driver");

  std::string type = driver->getString("type");
  log().info(fmt::format("  loading tracing driver: {}", type));

  Json::ObjectSharedPtr driver_config = driver->getObject("config");

  // Now see if there is a factory that will accept the config.
  auto search_it = httpTracerFactories().find(type);
  if (search_it != httpTracerFactories().end()) {
    http_tracer_ = search_it->second->createHttpTracer(*driver_config, server_, *cluster_manager_);
  } else {
    throw EnvoyException(fmt::format("No HttpTracerFactory found for type: {}", type));
  }
}

const std::list<Server::Configuration::ListenerPtr>& MainImpl::listeners() { return listeners_; }

MainImpl::ListenerConfig::ListenerConfig(MainImpl& parent, Json::Object& json) : parent_(parent) {
  address_ = Network::Utility::resolveUrl(json.getString("address"));

  // ':' is a reserved char in statsd. Do the translation here to avoid costly inline translations
  // later.
  std::string final_stat_name = fmt::format("listener.{}.", address_->asString());
  std::replace(final_stat_name.begin(), final_stat_name.end(), ':', '_');

  scope_ = parent_.server_.stats().createScope(final_stat_name);
  log().info("  address={}", address_->asString());

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

  std::vector<Json::ObjectSharedPtr> filters = json.getObjectArray("filters");
  for (size_t i = 0; i < filters.size(); i++) {
    std::string string_type = filters[i]->getString("type");
    std::string string_name = filters[i]->getString("name");
    Json::ObjectSharedPtr config = filters[i]->getObject("config");
    log().info("  filter #{}:", i);
    log().info("    type: {}", string_type);
    log().info("    name: {}", string_name);

    // Map filter type string to enum.
    NetworkFilterType type;
    if (string_type == "read") {
      type = NetworkFilterType::Read;
    } else if (string_type == "write") {
      type = NetworkFilterType::Write;
    } else {
      ASSERT(string_type == "both");
      type = NetworkFilterType::Both;
    }

    // Now see if there is a factory that will accept the config.
    auto search_it = namedFilterConfigFactories().find(string_name);
    if (search_it != namedFilterConfigFactories().end()) {
      NetworkFilterFactoryCb callback =
          search_it->second->createFilterFactory(type, *config, parent_.server_);
      filter_factories_.push_back(callback);
    } else {
      // DEPRECATED
      // This name wasn't found in the named map, so search in the deprecated list registry.
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
}

bool MainImpl::ListenerConfig::createFilterChain(Network::Connection& connection) {
  return FilterChainUtility::buildFilterChain(connection, filter_factories_);
}

InitialImpl::InitialImpl(const Json::Object& json) {
  json.validateSchema(Json::Schema::TOP_LEVEL_CONFIG_SCHEMA);
  Json::ObjectSharedPtr admin = json.getObject("admin");
  admin_.access_log_path_ = admin->getString("access_log_path");
  admin_.profile_path_ = admin->getString("profile_path", "/var/log/envoy/envoy.prof");
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
} // Envoy
