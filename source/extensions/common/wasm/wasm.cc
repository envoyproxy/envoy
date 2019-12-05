#include "extensions/common/wasm/wasm.h"

#include <stdio.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/wasm/v2/wasm.pb.validate.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/local_info/local_info.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/common/wasm/wasm_state.h"
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

std::atomic<int64_t> active_wasm_;

std::string base64Sha256(absl::string_view data) {
  std::vector<uint8_t> digest(SHA256_DIGEST_LENGTH);
  EVP_MD_CTX* ctx(EVP_MD_CTX_new());
  auto rc = EVP_DigestInit(ctx, EVP_sha256());
  RELEASE_ASSERT(rc == 1, "Failed to init digest context");
  rc = EVP_DigestUpdate(ctx, data.data(), data.size());
  RELEASE_ASSERT(rc == 1, "Failed to update digest");
  rc = EVP_DigestFinal(ctx, digest.data(), nullptr);
  RELEASE_ASSERT(rc == 1, "Failed to finalize digest");
  EVP_MD_CTX_free(ctx);
  return Base64::encode(reinterpret_cast<const char*>(&digest[0]), digest.size());
}

// Map from Wasm ID to the local Wasm instance.
thread_local absl::flat_hash_map<std::string, std::weak_ptr<WasmHandle>> local_wasms;

const std::string INLINE_STRING = "<inline>";

const uint8_t* decodeVarint(const uint8_t* pos, const uint8_t* end, uint32_t* out) {
  uint32_t ret = 0;
  int shift = 0;
  while (pos < end && (*pos & 0x80)) {
    ret |= (*pos & 0x7f) << shift;
    shift += 7;
    pos++;
  }
  if (pos < end) {
    ret |= *pos << shift;
    pos++;
  }
  *out = ret;
  return pos;
}

} // namespace

Wasm::Wasm(absl::string_view runtime, absl::string_view vm_id, absl::string_view vm_configuration,
           Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
           Event::Dispatcher& dispatcher)
    : vm_id_(std::string(vm_id)), wasm_vm_(Common::Wasm::createWasmVm(runtime, scope)),
      scope_(scope), cluster_manager_(cluster_manager), dispatcher_(dispatcher),
      time_source_(dispatcher.timeSource()), vm_configuration_(vm_configuration),
      wasm_stats_(WasmStats{
          ALL_WASM_STATS(POOL_COUNTER_PREFIX(*scope_, absl::StrCat("wasm.", runtime, ".")),
                         POOL_GAUGE_PREFIX(*scope_, absl::StrCat("wasm.", runtime, ".")))}),
      stat_name_set_(scope_->symbolTable().makeSet("Wasm").release()) {
  active_wasm_++;
  wasm_stats_.active_.set(active_wasm_);
  wasm_stats_.created_.inc();
  ENVOY_LOG(debug, "Base Wasm created {} now active", active_wasm_);
}

void Wasm::registerCallbacks() {
#define _REGISTER(_fn)                                                                             \
  wasm_vm_->registerCallback(                                                                      \
      "env", #_fn, &Exports::_fn,                                                                  \
      &ConvertFunctionWordToUint32<decltype(Exports::_fn),                                         \
                                   Exports::_fn>::convertFunctionWordToUint32)
  if (is_emscripten_) {
    _REGISTER(pthread_equal);
  }
#undef _REGISTER

#define _REGISTER_WASI(_fn)                                                                        \
  wasm_vm_->registerCallback(                                                                      \
      "wasi_unstable", #_fn, &Exports::wasi_unstable_##_fn,                                        \
      &ConvertFunctionWordToUint32<decltype(Exports::wasi_unstable_##_fn),                         \
                                   Exports::wasi_unstable_##_fn>::convertFunctionWordToUint32)
  if (is_emscripten_) {
    _REGISTER_WASI(fd_write);
    _REGISTER_WASI(fd_seek);
    _REGISTER_WASI(fd_close);
    _REGISTER_WASI(environ_get);
    _REGISTER_WASI(environ_sizes_get);
    _REGISTER_WASI(proc_exit);
  }
#undef _REGISTER_WASI

  // Calls with the "proxy_" prefix.
#define _REGISTER_PROXY(_fn)                                                                       \
  wasm_vm_->registerCallback(                                                                      \
      "env", "proxy_" #_fn, &Exports::_fn,                                                         \
      &ConvertFunctionWordToUint32<decltype(Exports::_fn),                                         \
                                   Exports::_fn>::convertFunctionWordToUint32);
  _REGISTER_PROXY(log);

  _REGISTER_PROXY(get_configuration);

  _REGISTER_PROXY(get_current_time_nanoseconds);

  _REGISTER_PROXY(define_metric);
  _REGISTER_PROXY(increment_metric);
  _REGISTER_PROXY(record_metric);
  _REGISTER_PROXY(get_metric);

  _REGISTER_PROXY(done);
#undef _REGISTER_PROXY
}

void Wasm::getFunctions() {
#define _GET(_fn) wasm_vm_->getFunction(#_fn, &_fn##_);
  _GET(_start);
  _GET(__wasm_call_ctors);

  _GET(malloc);
  _GET(free);
#undef _GET

#define _GET_PROXY(_fn) wasm_vm_->getFunction("proxy_" #_fn, &_fn##_);
  _GET_PROXY(validate_configuration);
  _GET_PROXY(on_start);
  _GET_PROXY(on_configure);

  _GET_PROXY(on_create);

  _GET_PROXY(on_done);
  _GET_PROXY(on_delete);
#undef _GET_PROXY

  if (!malloc_ || !free_) {
    throw WasmException("WASM missing malloc/free");
  }
}

Wasm::Wasm(const Wasm& wasm, Event::Dispatcher& dispatcher)
    : std::enable_shared_from_this<Wasm>(wasm), vm_id_(wasm.vm_id_),
      vm_id_with_hash_(wasm.vm_id_with_hash_), scope_(wasm.scope_),
      cluster_manager_(wasm.cluster_manager_), dispatcher_(dispatcher),
      time_source_(dispatcher.timeSource()), wasm_stats_(wasm.wasm_stats_),
      stat_name_set_(wasm.stat_name_set_) {
  if (started_from_ != Cloneable::NotCloneable) {
    wasm_vm_ = wasm.wasm_vm()->clone();
  } else {
    wasm_vm_ = Common::Wasm::createWasmVm(wasm.wasm_vm()->runtime(), scope_);
  }
  if (!initialize(wasm.code(), wasm.allow_precompiled())) {
    throw WasmException("Failed to initialize WASM code");
  }
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

bool Wasm::initialize(const std::string& code, bool allow_precompiled) {
  if (!wasm_vm_) {
    return false;
  }
  if (started_from_ == Cloneable::NotCloneable) {
    // Construct a unique identifier for the VM based on the provided vm_id and a hash of the
    // code.
    vm_id_with_hash_ = vm_id_ + ":" + base64Sha256(code);

    auto ok = wasm_vm_->load(code, allow_precompiled);
    if (!ok) {
      return false;
    }
    auto metadata = wasm_vm_->getCustomSection("emscripten_metadata");
    if (!metadata.empty()) {
      // See https://github.com/emscripten-core/emscripten/blob/incoming/tools/shared.py#L3059
      is_emscripten_ = true;
      auto start = reinterpret_cast<const uint8_t*>(metadata.data());
      auto end = reinterpret_cast<const uint8_t*>(metadata.data() + metadata.size());
      start = decodeVarint(start, end, &emscripten_metadata_major_version_);
      start = decodeVarint(start, end, &emscripten_metadata_minor_version_);
      start = decodeVarint(start, end, &emscripten_abi_major_version_);
      start = decodeVarint(start, end, &emscripten_abi_minor_version_);
      uint32_t temp;
      if (emscripten_metadata_major_version_ > 0 || emscripten_metadata_minor_version_ > 1) {
        // metadata 0.2 - added: wasm_backend.
        start = decodeVarint(start, end, &temp);
      }
      start = decodeVarint(start, end, &temp);
      start = decodeVarint(start, end, &temp);
      if (emscripten_metadata_major_version_ > 0 || emscripten_metadata_minor_version_ > 0) {
        // metadata 0.1 - added: global_base, dynamic_base, dynamictop_ptr and tempdouble_ptr.
        start = decodeVarint(start, end, &temp);
        start = decodeVarint(start, end, &temp);
        start = decodeVarint(start, end, &temp);
        decodeVarint(start, end, &temp);
        if (emscripten_metadata_major_version_ > 0 || emscripten_metadata_minor_version_ > 2) {
          // metadata 0.3 - added: standalone_wasm.
          start = decodeVarint(start, end, &emscripten_standalone_wasm_);
        }
      }
    }

    code_ = code;
    allow_precompiled_ = allow_precompiled;
  }

  if (started_from_ != Cloneable::InstantiatedModule) {
    registerCallbacks();
    wasm_vm_->link(vm_id_);
  }

  vm_context_ = std::make_shared<Context>(this);
  getFunctions();

  if (started_from_ != Cloneable::InstantiatedModule) {
    // Base VM was already started, so don't try to start cloned VMs again.
    startVm(vm_context_.get());
  }

  return true;
}

void Wasm::startVm(Context* root_context) {
  /* Call "_start" function, and fallback to "__wasm_call_ctors" if the former is not available. */
  if (_start_) {
    _start_(root_context);
  } else if (__wasm_call_ctors_) {
    __wasm_call_ctors_(root_context);
  }
}

bool Wasm::configure(Context* root_context, PluginSharedPtr plugin,
                     absl::string_view configuration) {
  return root_context->onConfigure(configuration, plugin);
}

Context* Wasm::start(PluginSharedPtr plugin) {
  auto root_id = plugin->root_id_;
  auto it = root_contexts_.find(root_id);
  if (it != root_contexts_.end()) {
    it->second->onStart(vm_configuration(), plugin);
    return it->second.get();
  }
  auto context = std::make_unique<Context>(this, root_id, plugin);
  auto context_ptr = context.get();
  root_contexts_[root_id] = std::move(context);
  context_ptr->onStart(vm_configuration(), plugin);
  return context_ptr;
};

void Wasm::startForTesting(std::unique_ptr<Context> context, PluginSharedPtr plugin) {
  auto context_ptr = context.get();
  if (!context->wasm_) {
    // Initialization was delayed till the Wasm object was created.
    context->wasm_ = this;
    context->plugin_ = plugin;
    context->id_ = allocContextId();
    contexts_[context->id_] = context.get();
  }
  root_contexts_[""] = std::move(context);
  context_ptr->onStart(vm_configuration_, plugin);
}

uint32_t Wasm::allocContextId() {
  while (true) {
    auto id = next_context_id_++;
    // Prevent reuse.
    if (contexts_.find(id) == contexts_.end()) {
      return id;
    }
  }
}

class Wasm::ShutdownHandle : public Envoy::Event::DeferredDeletable {
public:
  ShutdownHandle(WasmSharedPtr wasm) : wasm_(wasm) {}

private:
  WasmSharedPtr wasm_;
};

void Wasm::shutdown() {
  bool all_done = true;
  for (auto& p : root_contexts_) {
    if (!p.second->onDone()) {
      all_done = false;
      pending_done_.insert(p.second.get());
    }
  }
  if (!all_done) {
    shutdown_handle_ = std::make_unique<ShutdownHandle>(shared_from_this());
  }
}

WasmResult Wasm::done(Context* root_context) {
  auto it = pending_done_.find(root_context);
  if (it == pending_done_.end()) {
    return WasmResult::NotFound;
  }
  pending_done_.erase(it);
  if (pending_done_.empty()) {
    for (auto& p : root_contexts_) {
      p.second->onDelete();
    }
    dispatcher_.deferredDelete(std::move(shutdown_handle_));
  }
  return WasmResult::Ok;
}

static void createWasmInternal(const envoy::config::wasm::v2::VmConfig& vm_config,
                               PluginSharedPtr plugin, Stats::ScopeSharedPtr scope,
                               Upstream::ClusterManager& cluster_manager,
                               Init::Manager& init_manager, Event::Dispatcher& dispatcher,
                               Api::Api& api, std::unique_ptr<Context> root_context_for_testing,
                               Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                               CreateWasmCallback&& cb) {
  auto wasm = std::make_shared<WasmHandle>(
      std::make_shared<Wasm>(vm_config.runtime(), vm_config.vm_id(), vm_config.configuration(),
                             scope, cluster_manager, dispatcher));

  std::string source, code;
  if (vm_config.code().has_remote()) {
    source = vm_config.code().remote().http_uri().uri();
  } else if (vm_config.code().has_local()) {
    code = Config::DataSource::read(vm_config.code().local(), true, api);
    source = Config::DataSource::getPath(vm_config.code().local())
                 .value_or(code.empty() ? EMPTY_STRING : INLINE_STRING);
  }

  auto callback = [wasm, plugin, cb, source, allow_precompiled = vm_config.allow_precompiled(),
                   context_ptr = root_context_for_testing ? root_context_for_testing.release()
                                                          : nullptr](const std::string& code) {
    std::unique_ptr<Context> context(context_ptr);
    if (code.empty()) {
      throw WasmException(fmt::format("Failed to load WASM code from {}", source));
    }
    if (!wasm->wasm()->initialize(code, allow_precompiled)) {
      throw WasmException(fmt::format("Failed to initialize WASM code from {}", source));
    }
    if (!context) {
      wasm->wasm()->start(plugin);
    } else {
      wasm->wasm()->startForTesting(std::move(context), plugin);
    }
    cb(wasm);
  };

  if (vm_config.code().has_remote()) {
    remote_data_provider = std::make_unique<Config::DataSource::RemoteAsyncDataProvider>(
        cluster_manager, init_manager, vm_config.code().remote(), true, std::move(callback));
  } else if (vm_config.code().has_local()) {
    callback(code);
  } else {
    callback(EMPTY_STRING);
  }
}

void createWasm(const envoy::config::wasm::v2::VmConfig& vm_config, PluginSharedPtr plugin,
                Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
                Init::Manager& init_manager, Event::Dispatcher& dispatcher, Api::Api& api,
                Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                CreateWasmCallback&& cb) {
  createWasmInternal(vm_config, plugin, scope, cluster_manager, init_manager, dispatcher, api,
                     nullptr /* root_context_for_testing */, remote_data_provider, std::move(cb));
}

void createWasmForTesting(const envoy::config::wasm::v2::VmConfig& vm_config,
                          PluginSharedPtr plugin, Stats::ScopeSharedPtr scope,
                          Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                          Event::Dispatcher& dispatcher, Api::Api& api,
                          std::unique_ptr<Context> root_context_for_testing,
                          Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                          CreateWasmCallback&& cb) {
  createWasmInternal(vm_config, plugin, scope, cluster_manager, init_manager, dispatcher, api,
                     std::move(root_context_for_testing), remote_data_provider, std::move(cb));
}

WasmHandleSharedPtr createThreadLocalWasm(WasmHandle& base_wasm, PluginSharedPtr plugin,
                                          absl::string_view configuration,
                                          Event::Dispatcher& dispatcher) {
  auto wasm = std::make_shared<WasmHandle>(std::make_shared<Wasm>(*base_wasm.wasm(), dispatcher));
  Context* root_context = wasm->wasm()->start(plugin);
  if (!wasm->wasm()->configure(root_context, plugin, configuration)) {
    throw WasmException("Failed to configure WASM code");
  }
  local_wasms[wasm->wasm()->vm_id_with_hash()] = wasm;
  return wasm;
}

WasmHandleSharedPtr getThreadLocalWasmPtr(absl::string_view vm_id) {
  auto it = local_wasms.find(vm_id);
  if (it == local_wasms.end()) {
    return nullptr;
  }
  auto wasm = it->second.lock();
  if (!wasm) {
    local_wasms.erase(vm_id);
  }
  return wasm;
}

WasmHandleSharedPtr getOrCreateThreadLocalWasm(WasmHandle& base_wasm, PluginSharedPtr plugin,
                                               absl::string_view configuration,
                                               Event::Dispatcher& dispatcher) {
  auto wasm = getThreadLocalWasmPtr(base_wasm.wasm()->vm_id_with_hash());
  if (wasm) {
    auto root_context = wasm->wasm()->start(plugin);
    if (!wasm->wasm()->configure(root_context, plugin, configuration)) {
      throw WasmException("Failed to configure WASM code");
    }
    return wasm;
  }
  return createThreadLocalWasm(base_wasm, plugin, configuration, dispatcher);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
