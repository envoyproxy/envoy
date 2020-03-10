#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/config/datasource.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/common/wasm/context.h"
#include "extensions/common/wasm/wasm_vm.h"
#include "extensions/common/wasm/well_known_names.h"

#include "include/proxy-wasm/exports.h"
#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

#define ALL_WASM_STATS(COUNTER, GAUGE)                                                             \
  COUNTER(created)                                                                                 \
  GAUGE(active, NeverImport)

struct WasmStats {
  ALL_WASM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

using VmConfig = envoy::extensions::wasm::v3::VmConfig;

// Wasm execution instance. Manages the Envoy side of the Wasm interface.
class Wasm : public WasmBase, Logger::Loggable<Logger::Id::wasm> {
public:
  Wasm(absl::string_view runtime, absl::string_view vm_id, absl::string_view vm_configuration,
       absl::string_view vm_key, Stats::ScopeSharedPtr scope,
       Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher);
  Wasm(std::shared_ptr<WasmHandle>& other, Event::Dispatcher& dispatcher);
  virtual ~Wasm();

  Stats::StatNameSetSharedPtr stat_name_set() const { return stat_name_set_; }

  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  void error(absl::string_view message) override { throw WasmException(std::string(message)); }

private:
  friend class Context;

  Stats::ScopeSharedPtr scope_;
  Upstream::ClusterManager& cluster_manager_;
  Event::Dispatcher& dispatcher_;
  TimeSource& time_source_;

  // Host Stats/Metrics
  WasmStats wasm_stats_;

  // Plugin Stats/Metrics
  Stats::StatNameSetSharedPtr stat_name_set_;
  absl::flat_hash_map<uint32_t, Stats::Counter*> counters_;
  absl::flat_hash_map<uint32_t, Stats::Gauge*> gauges_;
  absl::flat_hash_map<uint32_t, Stats::Histogram*> histograms_;
};
using WasmSharedPtr = std::shared_ptr<Wasm>;

using WasmHandleSharedPtr = std::shared_ptr<WasmHandle>;
using CreateWasmCallback = std::function<void(WasmHandleSharedPtr)>;

// Create a high level Wasm VM with Envoy API support. Note: 'id' may be empty if this VM will not
// be shared by APIs (e.g. HTTP Filter + AccessLog).
void createWasm(const VmConfig& vm_config, PluginSharedPtr plugin_config,
                Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
                Init::Manager& init_manager, Event::Dispatcher& dispatcher, Api::Api& api,
                Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                CreateWasmCallback&& cb);

// Create a ThreadLocal VM from an existing VM (e.g. from createWasm() above).
WasmHandleSharedPtr createThreadLocalWasm(WasmHandle& base_wasm_handle, PluginSharedPtr plugin,
                                          Event::Dispatcher& dispatcher);

void createWasmForTesting(const VmConfig& vm_config, PluginSharedPtr plugin,
                          Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
                          Init::Manager& init_manager, Event::Dispatcher& dispatcher, Api::Api& api,
                          std::unique_ptr<Context> root_context_for_testing,
                          Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                          CreateWasmCallback&& cb);

WasmHandleSharedPtr getOrCreateThreadLocalWasm(WasmHandleSharedPtr base_wasm,
                                               PluginSharedPtr plugin,
                                               Event::Dispatcher& dispatcher);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
