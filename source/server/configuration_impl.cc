#include "server/configuration_impl.h"

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/context_manager.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/json/config_schemas.h"
#include "common/ratelimit/ratelimit_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {
namespace Configuration {

bool FilterChainUtility::buildFilterChain(Network::FilterManager& filter_manager,
                                          const std::vector<NetworkFilterFactoryCb>& factories) {
  for (const NetworkFilterFactoryCb& factory : factories) {
    factory(filter_manager);
  }

  return filter_manager.initializeReadFilters();
}

void MainImpl::initialize(const Json::Object& json, Instance& server,
                          Upstream::ClusterManagerFactory& cluster_manager_factory) {
  cluster_manager_ = cluster_manager_factory.clusterManagerFromJson(
      *json.getObject("cluster_manager"), server.stats(), server.threadLocal(), server.runtime(),
      server.random(), server.localInfo(), server.accessLogManager());

  std::vector<Json::ObjectSharedPtr> listeners = json.getObjectArray("listeners");
  ENVOY_LOG(info, "loading {} listener(s)", listeners.size());
  for (size_t i = 0; i < listeners.size(); i++) {
    ENVOY_LOG(info, "listener #{}:", i);
    server.listenerManager().addOrUpdateListener(*listeners[i]);
  }

  if (json.hasObject("lds")) {
    lds_api_.reset(new LdsApi(*json.getObject("lds"), *cluster_manager_, server.dispatcher(),
                              server.random(), server.initManager(), server.localInfo(),
                              server.stats(), server.listenerManager()));
  }

  if (json.hasObject("statsd_local_udp_port") && json.hasObject("statsd_udp_ip_address")) {
    throw EnvoyException("statsd_local_udp_port and statsd_udp_ip_address "
                         "are mutually exclusive.");
  }

  // TODO(hennna): DEPRECATED - statsd_local_udp_port will be removed in 1.4.0.
  if (json.hasObject("statsd_local_udp_port")) {
    statsd_udp_port_.value(json.getInteger("statsd_local_udp_port"));
  }

  if (json.hasObject("statsd_udp_ip_address")) {
    statsd_udp_ip_address_.value(json.getString("statsd_udp_ip_address"));
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

  initializeTracers(json, server);

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

void MainImpl::initializeTracers(const Json::Object& configuration, Instance& server) {
  ENVOY_LOG(info, "loading tracing configuration");

  if (!configuration.hasObject("tracing")) {
    http_tracer_.reset(new Tracing::HttpNullTracer());
    return;
  }

  Json::ObjectSharedPtr tracing_configuration = configuration.getObject("tracing");
  if (!tracing_configuration->hasObject("http")) {
    http_tracer_.reset(new Tracing::HttpNullTracer());
    return;
  }

  if (server.localInfo().clusterName().empty()) {
    throw EnvoyException("cluster name must be defined if tracing is enabled. See "
                         "--service-cluster option.");
  }

  // Initialize tracing driver.
  Json::ObjectSharedPtr http_tracer_config = tracing_configuration->getObject("http");
  Json::ObjectSharedPtr driver = http_tracer_config->getObject("driver");

  std::string type = driver->getString("type");
  ENVOY_LOG(info, "  loading tracing driver: {}", type);

  Json::ObjectSharedPtr driver_config = driver->getObject("config");

  // Now see if there is a factory that will accept the config.
  HttpTracerFactory* factory = Registry::FactoryRegistry<HttpTracerFactory>::getFactory(type);
  if (factory != nullptr) {
    http_tracer_ = factory->createHttpTracer(*driver_config, server, *cluster_manager_);
  } else {
    throw EnvoyException(fmt::format("No HttpTracerFactory found for type: {}", type));
  }
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

} // namespace Configuration
} // namespace Server
} // namespace Envoy
