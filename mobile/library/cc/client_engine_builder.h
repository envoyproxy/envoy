#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
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


#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace Envoy {
namespace Platform {

// The C++ client engine builder creates a semi-structured bootstrap proto and modifies it through parameters
// set through the ClientEngineBuilder API calls to produce the Bootstrap config that the Engine is
// created from. This differs from the EngineBuilder which is intended to be used for mobile environments and uses a more rigid bootstrap.
class ClientEngineBuilder {
public:
  ClientEngineBuilder();
  ClientEngineBuilder(ClientEngineBuilder&&) = default;
  virtual ~ClientEngineBuilder() = default;

  ClientEngineBuilder& addCluster(envoy::config::cluster::v3::Cluster&& cluster);
  ClientEngineBuilder& addHcmHttpFilter(envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter&& filter);
  ClientEngineBuilder& setHcmRouteConfiguration(envoy::config::route::v3::RouteConfiguration&& route_configuration);
  ClientEngineBuilder& setPerConnectionBufferLimit(uint32_t limit);
  ClientEngineBuilder& addStatsPrefix(std::string&& prefix);
  ClientEngineBuilder& setWatchdog(envoy::config::bootstrap::v3::Watchdogs&& watchdog);
  ClientEngineBuilder& setRouterConfig(envoy::extensions::filters::http::router::v3::Router&& router_config);

  ClientEngineBuilder& setLogLevel(Logger::Logger::Levels log_level);
  ClientEngineBuilder& setLogger(std::unique_ptr<EnvoyLogger> logger);
  ClientEngineBuilder& enableLogger(bool logger_on);
  ClientEngineBuilder& setEngineCallbacks(std::unique_ptr<EngineCallbacks> callbacks);
  ClientEngineBuilder& setOnEngineRunning(absl::AnyInvocable<void()> closure);
  ClientEngineBuilder& setOnEngineExit(absl::AnyInvocable<void()> closure);
  ClientEngineBuilder& setEventTracker(std::unique_ptr<EnvoyEventTracker> event_tracker);
  ClientEngineBuilder& setHcmStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds);

  ClientEngineBuilder& addRuntimeGuard(std::string guard, bool value);
  // Adds a runtime guard for the `envoy.restart_features.<guard>`. Restart features cannot be
  // changed after the Envoy applicable has started and initialized.
  // For example if the runtime guard is `envoy.restart_features.use_foo`, the guard name is
  // `use_foo`.
  ClientEngineBuilder& addRestartRuntimeGuard(std::string guard, bool value);

  // These functions don't affect the Bootstrap configuration but instead perform registrations.
  ClientEngineBuilder& addKeyValueStore(std::string name, KeyValueStoreSharedPtr key_value_store);
  ClientEngineBuilder& addStringAccessor(std::string name, StringAccessorSharedPtr accessor);

  // Sets the thread priority of the Envoy main (network) thread.
  // The value must be an integer between -20 (highest priority) and 19 (lowest priority). Values
  // outside of this range will be ignored.
  ClientEngineBuilder& setNetworkThreadPriority(int thread_priority);
  // Sets the high watermark for the response buffer. The low watermark is set to half of this
  // value. Defaults to 2MB if not set.
  ClientEngineBuilder& setBufferHighWatermark(size_t high_watermark);

  // Sets the node.id field in the Bootstrap configuration.
  ClientEngineBuilder& setNodeId(std::string node_id);
  // Sets the node.locality field in the Bootstrap configuration.
  ClientEngineBuilder& setNodeLocality(std::string region, std::string zone, std::string sub_zone);
  // Sets the node.metadata field in the Bootstrap configuration.
  ClientEngineBuilder& setNodeMetadata(Protobuf::Struct node_metadata);
  // Sets whether to collect Envoy's internal stats (counters & guages). Off by default.
  ClientEngineBuilder& enableStatsCollection(bool stats_collection_on);

  // This is separated from build() for the sake of testability
  virtual std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> generateBootstrap();

  EngineSharedPtr build();

private:
  Logger::Logger::Levels log_level_ = Logger::Logger::Levels::info;
  std::unique_ptr<EnvoyLogger> logger_ = nullptr;
  bool enable_logger_ = false;
  std::unique_ptr<EngineCallbacks> callbacks_ = nullptr;
  std::unique_ptr<EnvoyEventTracker> event_tracker_ = nullptr;

  int stream_idle_timeout_seconds_ = 15;
  absl::optional<int> network_thread_priority_ = absl::nullopt;
  absl::optional<size_t> high_watermark_ = absl::nullopt;

  absl::flat_hash_map<std::string, KeyValueStoreSharedPtr> key_value_stores_ = {};

  std::vector<envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter > hcm_http_filters_ = {};
  std::vector<envoy::config::cluster::v3::Cluster> clusters_ = {};
  std::unique_ptr<envoy::config::route::v3::RouteConfiguration> route_configuration_ = nullptr;
  uint32_t per_connection_buffer_limit_bytes_ = 10 * 1024 * 1024;
  std::vector<std::string> stats_prefixes_ = {};
  std::optional<envoy::config::bootstrap::v3::Watchdogs> watchdog_ = std::nullopt;
  std::optional<envoy::extensions::filters::http::router::v3::Router> router_config_ = std::nullopt;

  std::vector<std::pair<std::string, bool>> runtime_guards_ = {};
  std::vector<std::pair<std::string, bool>> restart_runtime_guards_ = {};
  absl::flat_hash_map<std::string, StringAccessorSharedPtr> string_accessors_ = {};

  std::string node_id_ = "";
  absl::optional<NodeLocality> node_locality_ = absl::nullopt;
  absl::optional<Protobuf::Struct> node_metadata_ = absl::nullopt;
  bool enable_stats_collection_ = false;
};

using ClientEngineBuilderSharedPtr = std::shared_ptr<ClientEngineBuilder>;

} // namespace Platform
} // namespace Envoy

#endif // ENVOY_ENABLE_FULL_PROTOS
