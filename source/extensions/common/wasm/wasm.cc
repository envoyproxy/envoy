#include "extensions/common/wasm/wasm.h"

#include <stdio.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/common/wasm/wasm_vm.h"
#include "extensions/common/wasm/well_known_names.h"

#include "absl/base/casts.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "openssl/bytestring.h"
#include "openssl/hmac.h"
#include "openssl/sha.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

namespace {

const std::string INLINE_STRING = "<inline>";

std::atomic<int64_t> active_wasm_;

// Downcast WasmBase to the actual Wasm.
inline Wasm* getWasm(WasmHandleSharedPtr& base_wasm_handle) {
  return static_cast<Wasm*>(base_wasm_handle->wasm().get());
}

} // namespace

Wasm::Wasm(absl::string_view runtime, absl::string_view vm_id, absl::string_view vm_configuration,
           absl::string_view vm_key, Stats::ScopeSharedPtr scope,
           Upstream::ClusterManager& cluster_manager, Event::Dispatcher& dispatcher)
    : WasmBase(createWasmVm(runtime, scope), vm_id, vm_configuration, vm_key), scope_(scope),
      cluster_manager_(cluster_manager), dispatcher_(dispatcher),
      time_source_(dispatcher.timeSource()),
      wasm_stats_(WasmStats{
          ALL_WASM_STATS(POOL_COUNTER_PREFIX(*scope_, absl::StrCat("wasm.", runtime, ".")),
                         POOL_GAUGE_PREFIX(*scope_, absl::StrCat("wasm.", runtime, ".")))}),
      stat_name_set_(scope_->symbolTable().makeSet("Wasm").release()) {
  active_wasm_++;
  wasm_stats_.active_.set(active_wasm_);
  wasm_stats_.created_.inc();
  ENVOY_LOG(debug, "Base Wasm created {} now active", active_wasm_);
}

Wasm::Wasm(WasmHandleSharedPtr& base_wasm_handle, Event::Dispatcher& dispatcher)
    : WasmBase(base_wasm_handle,
               [&base_wasm_handle]() {
                 return createWasmVm(
                     getEnvoyWasmIntegration(*base_wasm_handle->wasm()->wasm_vm()).runtime(),
                     getWasm(base_wasm_handle)->scope_);
               }),
      scope_(getWasm(base_wasm_handle)->scope_),
      cluster_manager_(getWasm(base_wasm_handle)->clusterManager()), dispatcher_(dispatcher),
      time_source_(dispatcher.timeSource()), wasm_stats_(getWasm(base_wasm_handle)->wasm_stats_),
      stat_name_set_(getWasm(base_wasm_handle)->stat_name_set()) {
  active_wasm_++;
  wasm_stats_.active_.set(active_wasm_);
  wasm_stats_.created_.inc();
  ENVOY_LOG(debug, "Thread-Local Wasm created {} now active", active_wasm_);
}

Wasm::~Wasm() {
  active_wasm_--;
  wasm_stats_.active_.set(active_wasm_);
  ENVOY_LOG(debug, "~Wasm {} remaining active", active_wasm_);
}

static void createWasmInternal(const VmConfig& vm_config, PluginSharedPtr plugin,
                               Stats::ScopeSharedPtr scope,
                               Upstream::ClusterManager& cluster_manager,
                               Init::Manager& init_manager, Event::Dispatcher& dispatcher,
                               Runtime::RandomGenerator& random, Api::Api& api,
                               std::unique_ptr<Context> root_context_for_testing,
                               Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                               CreateWasmCallback&& cb) {
  std::string source, code;
  if (vm_config.code().has_remote()) {
    source = vm_config.code().remote().http_uri().uri();
  } else if (vm_config.code().has_local()) {
    code = Config::DataSource::read(vm_config.code().local(), true, api);
    source = Config::DataSource::getPath(vm_config.code().local())
                 .value_or(code.empty() ? EMPTY_STRING : INLINE_STRING);
  }

  auto callback = [vm_config, scope, &cluster_manager, &dispatcher, plugin, cb, source,
                   context_ptr = root_context_for_testing ? root_context_for_testing.release()
                                                          : nullptr](const std::string& code) {
    std::unique_ptr<Context> context(context_ptr);
    if (code.empty()) {
      throw WasmException(fmt::format("Failed to load WASM code from {}", source));
    }
    std::string configuration;
    if (!vm_config.configuration().SerializeToString(&configuration)) {
      throw WasmException(fmt::format("Failed to serialize vm configuration"));
    }
    auto vm_key = proxy_wasm::makeVmKey(vm_config.vm_id(), configuration, code);
    cb(proxy_wasm::createWasm(
        vm_key, code, plugin,
        [&vm_config, &configuration, &scope, &cluster_manager,
         &dispatcher](absl::string_view vm_key) {
          return std::make_shared<WasmHandle>(
              std::make_shared<Wasm>(vm_config.runtime(), vm_config.vm_id(), configuration, vm_key,
                                     scope, cluster_manager, dispatcher));
        },
        vm_config.allow_precompiled(), std::move(context)));
  };

  if (vm_config.code().has_remote()) {
    remote_data_provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
        cluster_manager, init_manager, vm_config.code().remote(), dispatcher, random, true,
        std::move(callback));
  } else if (vm_config.code().has_local()) {
    callback(code);
  } else {
    callback(EMPTY_STRING);
  }
}

void createWasm(const VmConfig& vm_config, PluginSharedPtr plugin, Stats::ScopeSharedPtr scope,
                Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random, Api::Api& api,
                Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                CreateWasmCallback&& cb) {
  createWasmInternal(vm_config, plugin, scope, cluster_manager, init_manager, dispatcher, random,
                     api, nullptr /* root_context_for_testing */, remote_data_provider,
                     std::move(cb));
}

void createWasmForTesting(const VmConfig& vm_config, PluginSharedPtr plugin,
                          Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
                          Init::Manager& init_manager, Event::Dispatcher& dispatcher,
                          Runtime::RandomGenerator& random, Api::Api& api,
                          std::unique_ptr<Context> root_context_for_testing,
                          Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                          CreateWasmCallback&& cb) {
  createWasmInternal(vm_config, plugin, scope, cluster_manager, init_manager, dispatcher, random,
                     api, std::move(root_context_for_testing), remote_data_provider, std::move(cb));
}

WasmHandleSharedPtr getOrCreateThreadLocalWasm(WasmHandleSharedPtr base_wasm,
                                               PluginSharedPtr plugin,
                                               Event::Dispatcher& dispatcher) {
  auto wasm_handle = proxy_wasm::getOrCreateThreadLocalWasm(
      base_wasm, plugin, [&dispatcher](WasmHandleSharedPtr base_wasm) -> WasmHandleSharedPtr {
        return std::make_shared<WasmHandle>(std::make_shared<Wasm>(base_wasm, dispatcher));
      });
  if (!wasm_handle) {
    throw WasmException("Failed to configure WASM code");
  }
  return wasm_handle;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
