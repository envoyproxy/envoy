#include "client_engine_builder.h"

#include <stdint.h>
#include <sys/socket.h>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "source/common/protobuf/protobuf.h"
#include "library/cc/engine.h"
#include "library/cc/key_value_store.h"
#include "library/cc/string_accessor.h"
#include "library/common/engine_types.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "library/cc/engine_builder.h"
#include "library/cc/stream_client.h"
#include "source/common/common/base_logger.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "absl/debugging/leak_check.h"
#include "library/common/api/c_types.h"
#include "library/common/api/external.h"
#include "library/common/internal_engine.h"
#include "source/common/common/assert.h"
#include "source/server/options_impl_base.h"
#include "spdlog/spdlog.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace Envoy {
namespace Platform {

ClientEngineBuilder::ClientEngineBuilder() : callbacks_(std::make_unique<EngineCallbacks>()) {}

ClientEngineBuilder& ClientEngineBuilder::setNetworkThreadPriority(int thread_priority) {
  network_thread_priority_ = thread_priority;
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setBufferHighWatermark(size_t high_watermark) {
  high_watermark_ = high_watermark;
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setLogLevel(Logger::Logger::Levels log_level) {
  log_level_ = log_level;
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setLogger(std::unique_ptr<EnvoyLogger> logger) {
  logger_ = std::move(logger);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::enableLogger(bool logger_on) {
  enable_logger_ = logger_on;
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setEngineCallbacks(std::unique_ptr<EngineCallbacks> callbacks) {
  callbacks_ = std::move(callbacks);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setOnEngineRunning(absl::AnyInvocable<void()> closure) {
  callbacks_->on_engine_running_ = std::move(closure);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setOnEngineExit(absl::AnyInvocable<void()> closure) {
  callbacks_->on_exit_ = std::move(closure);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setEventTracker(std::unique_ptr<EnvoyEventTracker> event_tracker) {
  event_tracker_ = std::move(event_tracker);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::addKeyValueStore(std::string name,
                                               KeyValueStoreSharedPtr key_value_store) {
  key_value_stores_[std::move(name)] = std::move(key_value_store);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setHcmStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds) {
  stream_idle_timeout_seconds_ = stream_idle_timeout_seconds;
  return *this;
}


ClientEngineBuilder& ClientEngineBuilder::addStringAccessor(std::string name,
                                                StringAccessorSharedPtr accessor) {
  string_accessors_[std::move(name)] = std::move(accessor);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::addCluster(envoy::config::cluster::v3::Cluster&& cluster) {
  clusters_.push_back(std::move(cluster));
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::addHcmHttpFilter(envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&& filter) {
  hcm_http_filters_.push_back(std::move(filter));
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setHcmRouteConfiguration(envoy::config::route::v3::RouteConfiguration&& route_configuration) {
  route_configuration_ =
      std::make_unique<envoy::config::route::v3::RouteConfiguration>(
          std::move(route_configuration));
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setWatchdog(envoy::config::bootstrap::v3::Watchdogs&& watchdog) {
  watchdog_ = std::move(watchdog);
  return *this;
}
ClientEngineBuilder& ClientEngineBuilder::setRouterConfig(envoy::extensions::filters::http::router::v3::Router&& router_config) {
  router_config_ = std::move(router_config);
  return *this;
}
ClientEngineBuilder& ClientEngineBuilder::setPerConnectionBufferLimit(uint32_t limit) {
  per_connection_buffer_limit_bytes_ = limit;
  return *this;
}
ClientEngineBuilder& ClientEngineBuilder::addStatsPrefix(std::string&& prefix) {
  stats_prefixes_.push_back(std::move(prefix));
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::addRuntimeGuard(std::string guard, bool value) {
  runtime_guards_.emplace_back(std::move(guard), value);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::addRestartRuntimeGuard(std::string guard, bool value) {
  restart_runtime_guards_.emplace_back(std::move(guard), value);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::enableStatsCollection(bool stats_collection_on) {
  enable_stats_collection_ = stats_collection_on;
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setNodeId(std::string node_id) {
  node_id_ = std::move(node_id);
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setNodeLocality(std::string region, std::string zone,
                                              std::string sub_zone) {
  node_locality_ = {std::move(region), std::move(zone), std::move(sub_zone)};
  return *this;
}

ClientEngineBuilder& ClientEngineBuilder::setNodeMetadata(Protobuf::Struct node_metadata) {
  node_metadata_ = std::move(node_metadata);
  return *this;
}

std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> ClientEngineBuilder::generateBootstrap() {
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap =
      std::make_unique<envoy::config::bootstrap::v3::Bootstrap>();

  // Set up the HCM, make sure a route config is provided.
  envoy::extensions::filters::network::http_connection_manager::v3::EnvoyMobileHttpConnectionManager
      api_listener_config;
  auto* hcm = api_listener_config.mutable_config();
  hcm->set_stat_prefix("hcm");
  hcm->set_server_header_transformation(
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
          PASS_THROUGH);
  hcm->mutable_stream_idle_timeout()->set_seconds(stream_idle_timeout_seconds_);
  if (route_configuration_ == nullptr) {
    RELEASE_ASSERT(false, "ClientEngineBuilder::GenerateBootstrap called with no route configuration provided.");
  }
  *(hcm->mutable_route_config()) = *std::move(route_configuration_).get();
  route_configuration_.reset();

  // Add any provided HTTP filters to the HCM.
  for (auto filter : hcm_http_filters_) {
    *hcm->add_http_filters() = std::move(filter);
  }
  hcm_http_filters_.clear();

  // Add the router filter to the HCM last.
  auto* router_filter = hcm->add_http_filters();
  router_filter->set_name("envoy.router");
  if (router_config_.has_value()) {
    router_filter->mutable_typed_config()->PackFrom(*router_config_);
  } else {
    envoy::extensions::filters::http::router::v3::Router router_config;
    router_filter->mutable_typed_config()->PackFrom(router_config);
  }

  auto* static_resources = bootstrap->mutable_static_resources();

  // Finally create the base API listener, and point it at the HCM.
  auto* base_listener = static_resources->add_listeners();
  base_listener->set_name("base_api_listener");
  auto* base_address = base_listener->mutable_address();
  base_address->mutable_socket_address()->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  base_address->mutable_socket_address()->set_address("0.0.0.0");
  base_address->mutable_socket_address()->set_port_value(10000);
  base_listener->mutable_per_connection_buffer_limit_bytes()->set_value(per_connection_buffer_limit_bytes_);
  base_listener->mutable_api_listener()->mutable_api_listener()->PackFrom(api_listener_config);

  // Add all clusters to the bootstrap, there should be at least one.
  if (clusters_.empty()) {
    RELEASE_ASSERT(false, "ClientEngineBuilder::GenerateBootstrap called with no clusters provided.");
  }
  for (auto cluster : clusters_) {
    *static_resources->add_clusters() = std::move(cluster);
  }
  clusters_.clear();

  // Set up stats if provided.
  if (!stats_prefixes_.empty()) {
    auto* list =
        bootstrap->mutable_stats_config()->mutable_stats_matcher()->mutable_inclusion_list();
    for (const auto& prefix : stats_prefixes_) {
      list->add_patterns()->set_prefix(prefix);
    }
  } else {
    bootstrap->mutable_stats_config()->mutable_stats_matcher()->set_reject_all(true);
  }
  bootstrap->mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);

  // Set up watchdog if provided.
  if (watchdog_.has_value()) {
    *bootstrap->mutable_watchdogs() = std::move(watchdog_).value();
  }

  // Set up node.
  auto* node = bootstrap->mutable_node();
  node->set_id(node_id_.empty() ? "envoy-client" : node_id_);
  node->set_cluster("envoy-client");
  if (node_locality_ && !node_locality_->region.empty()) {
    node->mutable_locality()->set_region(node_locality_->region);
    node->mutable_locality()->set_zone(node_locality_->zone);
    node->mutable_locality()->set_sub_zone(node_locality_->sub_zone);
  }
  if (node_metadata_.has_value()) {
    *node->mutable_metadata() = *node_metadata_;
  }

  // Set up runtime.
  auto* runtime = bootstrap->mutable_layered_runtime()->add_layers();
  runtime->set_name("static_layer_0");
  Protobuf::Struct envoy_layer;
  Protobuf::Struct& runtime_values =
      *(*envoy_layer.mutable_fields())["envoy"].mutable_struct_value();
  Protobuf::Struct& reloadable_features =
      *(*runtime_values.mutable_fields())["reloadable_features"].mutable_struct_value();
  for (auto& guard_and_value : runtime_guards_) {
    (*reloadable_features.mutable_fields())[guard_and_value.first].set_bool_value(
        guard_and_value.second);
  }
  Protobuf::Struct& restart_features =
      *(*runtime_values.mutable_fields())["restart_features"].mutable_struct_value();
  (*runtime_values.mutable_fields())["disallow_global_stats"].set_bool_value(true);
  for (auto& guard_and_value : restart_runtime_guards_) {
    (*restart_features.mutable_fields())[guard_and_value.first].set_bool_value(
        guard_and_value.second);
  }
  Protobuf::Struct& overload_values =
      *(*envoy_layer.mutable_fields())["overload"].mutable_struct_value();
  (*overload_values.mutable_fields())["global_downstream_max_connections"].set_string_value(
      "4294967295");
  runtime->mutable_static_layer()->MergeFrom(envoy_layer);

  bootstrap->mutable_dynamic_resources();

  envoy::config::listener::v3::ApiListenerManager api;
  auto* listener_manager = bootstrap->mutable_listener_manager();
  listener_manager->mutable_typed_config()->PackFrom(api);
  listener_manager->set_name("envoy.listener_manager_impl.api");

  return bootstrap;
}

EngineSharedPtr ClientEngineBuilder::build() {
  InternalEngine* envoy_engine = absl::IgnoreLeak(
      new InternalEngine(std::move(callbacks_), std::move(logger_), std::move(event_tracker_),
                         network_thread_priority_, high_watermark_,
                         false, enable_logger_));

  for (const auto& [name, store] : key_value_stores_) {
    // TODO(goaway): This leaks, but it's tied to the life of the engine.
    if (!Api::External::retrieveApi(name, true)) {
      auto* api = new envoy_kv_store();
      *api = store->asEnvoyKeyValueStore();
      Envoy::Api::External::registerApi(name.c_str(), api);
    }
  }

  for (const auto& [name, accessor] : string_accessors_) {
    // TODO(RyanTheOptimist): This leaks, but it's tied to the life of the engine.
    if (!Api::External::retrieveApi(name, true)) {
      auto* api = new envoy_string_accessor();
      *api = StringAccessor::asEnvoyStringAccessor(accessor);
      Envoy::Api::External::registerApi(name.c_str(), api);
    }
  }

  Engine* engine = new Engine(envoy_engine);

  auto options = std::make_shared<Envoy::OptionsImplBase>();
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap = generateBootstrap();
  if (bootstrap) {
    options->setConfigProto(std::move(bootstrap));
  }
  options->setLogLevel(static_cast<spdlog::level::level_enum>(log_level_));
  options->setConcurrency(1);
  envoy_engine->run(options);

  // we can't construct via std::make_shared
  // because Engine is only constructible as a friend
  auto engine_ptr = EngineSharedPtr(engine);
  return engine_ptr;
}

} // namespace Platform
} // namespace Envoy

#endif // ENVOY_ENABLE_FULL_PROTOS
