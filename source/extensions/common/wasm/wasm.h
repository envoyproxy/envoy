#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/config/wasm/v2/wasm.pb.validate.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/stack_array.h"
#include "common/config/datasource.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/common/wasm/context.h"
#include "extensions/common/wasm/exports.h"
#include "extensions/common/wasm/wasm_vm.h"
#include "extensions/common/wasm/well_known_names.h"

namespace Envoy {

// TODO: move to source/common/stats/symbol_table_impl.h when upstreaming.
namespace Stats {
using StatNameSetSharedPtr = std::shared_ptr<Stats::StatNameSet>;
} // namespace Stats

namespace Extensions {
namespace Common {
namespace Wasm {

#define ALL_WASM_STATS(COUNTER, GAUGE)                                                             \
  COUNTER(created)                                                                                 \
  GAUGE(active, NeverImport)

struct WasmStats {
  ALL_WASM_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// Wasm execution instance. Manages the Envoy side of the Wasm interface.
class Wasm : public Logger::Loggable<Logger::Id::wasm>, public std::enable_shared_from_this<Wasm> {
public:
  Wasm(absl::string_view runtime, absl::string_view vm_id, absl::string_view vm_configuration,
       Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
       Event::Dispatcher& dispatcher);
  Wasm(const Wasm& other, Event::Dispatcher& dispatcher);
  ~Wasm();

  bool initialize(const std::string& code, bool allow_precompiled = false);
  void startVm(Context* root_context);
  bool configure(Context* root_context, PluginSharedPtr plugin, absl::string_view configuration);
  Context* start(PluginSharedPtr plugin); // returns the root Context.

  absl::string_view vm_id() const { return vm_id_; }
  absl::string_view vm_id_with_hash() const { return vm_id_with_hash_; }
  WasmVm* wasm_vm() const { return wasm_vm_.get(); }
  Context* vm_context() const { return vm_context_.get(); }
  Stats::StatNameSetSharedPtr stat_name_set() const { return stat_name_set_; }
  Context* getRootContext(absl::string_view root_id) { return root_contexts_[root_id].get(); }
  Context* getContext(uint32_t id) {
    auto it = contexts_.find(id);
    if (it != contexts_.end())
      return it->second;
    return nullptr;
  }
  uint32_t allocContextId();

  Upstream::ClusterManager& clusterManager() const { return cluster_manager_; }
  const std::string& code() const { return code_; }
  const std::string& vm_configuration() const { return vm_configuration_; }
  bool allow_precompiled() const { return allow_precompiled_; }
  void setInitialConfiguration(const std::string& vm_configuration) {
    vm_configuration_ = vm_configuration;
  }
  Event::Dispatcher& dispatcher() { return dispatcher_; }

  void queueReady(uint32_t root_context_id, uint32_t token);

  void shutdown();
  WasmResult done(Context* root_context);

  //
  // AccessLog::Instance
  //
  void log(absl::string_view root_id, const Http::HeaderMap* request_headers,
           const Http::HeaderMap* response_headers, const Http::HeaderMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info);

  // Support functions.
  void* allocMemory(uint64_t size, uint64_t* address);
  // Allocate a null-terminated string in the VM and return the pointer to use as a call arguments.
  uint64_t copyString(absl::string_view s);
  uint64_t copyBuffer(const Buffer::Instance& buffer);
  // Copy the data in 's' into the VM along with the pointer-size pair. Returns true on success.
  bool copyToPointerSize(absl::string_view s, uint64_t ptr_ptr, uint64_t size_ptr);
  bool copyToPointerSize(const Buffer::Instance& buffer, uint64_t start, uint64_t length,
                         uint64_t ptr_ptr, uint64_t size_ptr);
  template <typename T> bool setDatatype(uint64_t ptr, const T& t);

  // For testing.
  void setContext(Context* context) { contexts_[context->id()] = context; }
  void startForTesting(std::unique_ptr<Context> root_context, PluginSharedPtr plugin);

  bool getEmscriptenVersion(uint32_t* emscripten_metadata_major_version,
                            uint32_t* emscripten_metadata_minor_version,
                            uint32_t* emscripten_abi_major_version,
                            uint32_t* emscripten_abi_minor_version) {
    if (!is_emscripten_) {
      return false;
    }
    *emscripten_metadata_major_version = emscripten_metadata_major_version_;
    *emscripten_metadata_minor_version = emscripten_metadata_minor_version_;
    *emscripten_abi_major_version = emscripten_abi_major_version_;
    *emscripten_abi_minor_version = emscripten_abi_minor_version_;
    return true;
  }

private:
  friend class Context;
  class ShutdownHandle;
  // These are the same as the values of the Context::MetricType enum, here separately for
  // convenience.
  static const uint32_t kMetricTypeCounter = 0x0;
  static const uint32_t kMetricTypeGauge = 0x1;
  static const uint32_t kMetricTypeHistogram = 0x2;
  static const uint32_t kMetricTypeMask = 0x3;
  static const uint32_t kMetricIdIncrement = 0x4;
  static void StaticAsserts() {
    static_assert(static_cast<uint32_t>(Context::MetricType::Counter) == kMetricTypeCounter, "");
    static_assert(static_cast<uint32_t>(Context::MetricType::Gauge) == kMetricTypeGauge, "");
    static_assert(static_cast<uint32_t>(Context::MetricType::Histogram) == kMetricTypeHistogram,
                  "");
  }

  bool isCounterMetricId(uint32_t metric_id) {
    return (metric_id & kMetricTypeMask) == kMetricTypeCounter;
  }
  bool isGaugeMetricId(uint32_t metric_id) {
    return (metric_id & kMetricTypeMask) == kMetricTypeGauge;
  }
  bool isHistogramMetricId(uint32_t metric_id) {
    return (metric_id & kMetricTypeMask) == kMetricTypeHistogram;
  }
  uint32_t nextCounterMetricId() { return next_counter_metric_id_ += kMetricIdIncrement; }
  uint32_t nextGaugeMetricId() { return next_gauge_metric_id_ += kMetricIdIncrement; }
  uint32_t nextHistogramMetricId() { return next_histogram_metric_id_ += kMetricIdIncrement; }

  void registerCallbacks();    // Register functions called out from WASM.
  void establishEnvironment(); // Language specific environments.
  void getFunctions();         // Get functions call into WASM.

  std::string vm_id_;           // User-provided vm_id.
  std::string vm_id_with_hash_; // vm_id + hash of code.
  std::unique_ptr<WasmVm> wasm_vm_;
  Cloneable started_from_{Cloneable::NotCloneable};
  Stats::ScopeSharedPtr scope_;

  Upstream::ClusterManager& cluster_manager_;
  Event::Dispatcher& dispatcher_;

  uint32_t next_context_id_ = 1; // 0 is reserved for the VM context.
  ContextSharedPtr vm_context_;  // Context unrelated to any specific root or stream
                                 // (e.g. for global constructors).
  absl::flat_hash_map<std::string, std::unique_ptr<Context>> root_contexts_;
  absl::flat_hash_map<uint32_t, Context*> contexts_; // Contains all contexts.
  std::unique_ptr<ShutdownHandle> shutdown_handle_;
  absl::flat_hash_set<Context*> pending_done_; // Root contexts not done during shutdown.

  TimeSource& time_source_;

  WasmCallVoid<0> _start_; /* Emscripten v1.39.0+ */
  WasmCallVoid<0> __wasm_call_ctors_;

  WasmCallWord<1> malloc_;
  WasmCallVoid<1> free_;

  // Calls into the VM.
  WasmCallWord<2> validate_configuration_;
  WasmCallWord<2> on_start_;
  WasmCallWord<2> on_configure_;
  WasmCallVoid<1> on_tick_;

  WasmCallVoid<2> on_create_;

  WasmCallWord<1> on_new_connection_;
  WasmCallWord<3> on_downstream_data_;
  WasmCallWord<3> on_upstream_data_;
  WasmCallVoid<2> on_downstream_connection_close_;
  WasmCallVoid<2> on_upstream_connection_close_;

  WasmCallWord<2> on_request_headers_;
  WasmCallWord<3> on_request_body_;
  WasmCallWord<2> on_request_trailers_;
  WasmCallWord<2> on_request_metadata_;

  WasmCallWord<2> on_response_headers_;
  WasmCallWord<3> on_response_body_;
  WasmCallWord<2> on_response_trailers_;
  WasmCallWord<2> on_response_metadata_;

  WasmCallVoid<5> on_http_call_response_;

  WasmCallVoid<3> on_grpc_receive_;
  WasmCallVoid<3> on_grpc_close_;
  WasmCallVoid<3> on_grpc_create_initial_metadata_;
  WasmCallVoid<3> on_grpc_receive_initial_metadata_;
  WasmCallVoid<3> on_grpc_receive_trailing_metadata_;

  WasmCallVoid<2> on_queue_ready_;

  WasmCallWord<1> on_done_;
  WasmCallVoid<1> on_log_;
  WasmCallVoid<1> on_delete_;

  // Used by the base_wasm to enable non-clonable thread local Wasm(s) to be constructed.
  std::string code_;
  std::string vm_configuration_;
  bool allow_precompiled_ = false;

  bool is_emscripten_ = false;
  uint32_t emscripten_metadata_major_version_ = 0;
  uint32_t emscripten_metadata_minor_version_ = 0;
  uint32_t emscripten_abi_major_version_ = 0;
  uint32_t emscripten_abi_minor_version_ = 0;
  uint32_t emscripten_standalone_wasm_ = 0;

  // Host Stats/Metrics
  WasmStats wasm_stats_;

  // Plulgin Stats/Metrics
  Stats::StatNameSetSharedPtr stat_name_set_;
  uint32_t next_counter_metric_id_ = kMetricTypeCounter;
  uint32_t next_gauge_metric_id_ = kMetricTypeGauge;
  uint32_t next_histogram_metric_id_ = kMetricTypeHistogram;
  absl::flat_hash_map<uint32_t, Stats::Counter*> counters_;
  absl::flat_hash_map<uint32_t, Stats::Gauge*> gauges_;
  absl::flat_hash_map<uint32_t, Stats::Histogram*> histograms_;
};
using WasmSharedPtr = std::shared_ptr<Wasm>;

// Handle which enables shutdown operations to run post deletion (e.g. post listener drain).
class WasmHandle : public ThreadLocal::ThreadLocalObject,
                   public std::enable_shared_from_this<WasmHandle> {
public:
  explicit WasmHandle(WasmSharedPtr wasm) : wasm_(wasm) {}
  ~WasmHandle() {
    auto wasm = wasm_;
    // NB: V8 will stack overflow during the stress test if we shutdown with the call stack in the
    // ThreadLocal set call so shift to a fresh call stack.
    wasm_->dispatcher().post([wasm] { wasm->shutdown(); });
  }

  const WasmSharedPtr& wasm() { return wasm_; }

private:
  WasmSharedPtr wasm_;
};
using WasmHandleSharedPtr = std::shared_ptr<WasmHandle>;

using CreateWasmCallback = std::function<void(WasmHandleSharedPtr)>;

// Create a high level Wasm VM with Envoy API support. Note: 'id' may be empty if this VM will not
// be shared by APIs (e.g. HTTP Filter + AccessLog).
void createWasm(const envoy::config::wasm::v2::VmConfig& vm_config, PluginSharedPtr plugin_config,
                Stats::ScopeSharedPtr scope, Upstream::ClusterManager& cluster_manager,
                Init::Manager& init_manager, Event::Dispatcher& dispatcher, Api::Api& api,
                Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                CreateWasmCallback&& cb);

// Create a ThreadLocal VM from an existing VM (e.g. from createWasm() above).
WasmHandleSharedPtr createThreadLocalWasm(WasmHandle& base_wasm_handle, PluginSharedPtr plugin,
                                          absl::string_view configuration,
                                          Event::Dispatcher& dispatcher);

void createWasmForTesting(const envoy::config::wasm::v2::VmConfig& vm_config,
                          PluginSharedPtr plugin, Stats::ScopeSharedPtr scope,
                          Upstream::ClusterManager& cluster_manager, Init::Manager& init_manager,
                          Event::Dispatcher& dispatcher, Api::Api& api,
                          std::unique_ptr<Context> root_context_for_testing,
                          Config::DataSource::RemoteAsyncDataProviderPtr& remote_data_provider,
                          CreateWasmCallback&& cb);

// Get an existing ThreadLocal VM matching 'vm_id' or nullptr if there isn't one.
WasmHandleSharedPtr getThreadLocalWasmPtr(absl::string_view vm_id);
// Get an existing ThreadLocal VM matching 'vm_id' or create one using 'base_wavm' by cloning or by
// using it it as a template.
WasmHandleSharedPtr getOrCreateThreadLocalWasm(WasmHandle& base_wasm, PluginSharedPtr plugin,
                                               absl::string_view configuration,
                                               Event::Dispatcher& dispatcher);

inline void* Wasm::allocMemory(uint64_t size, uint64_t* address) {
  Word a = malloc_(vm_context(), size);
  if (!a.u64_) {
    throw WasmException("malloc_ returns nullptr (OOM)");
  }
  auto memory = wasm_vm_->getMemory(a.u64_, size);
  if (!memory) {
    throw WasmException("malloc_ returned illegal address");
  }
  *address = a.u64_;
  return const_cast<void*>(reinterpret_cast<const void*>(memory.value().data()));
}

inline uint64_t Wasm::copyString(absl::string_view s) {
  if (s.empty()) {
    return 0; // nullptr
  }
  uint64_t pointer;
  uint8_t* m = static_cast<uint8_t*>(allocMemory((s.size() + 1), &pointer));
  memcpy(m, s.data(), s.size());
  m[s.size()] = 0;
  return pointer;
}

inline uint64_t Wasm::copyBuffer(const Buffer::Instance& buffer) {
  uint64_t pointer;
  auto length = buffer.length();
  if (length <= 0) {
    return 0;
  }
  Buffer::RawSlice oneRawSlice;
  // NB: we need to pass in >= 1 in order to get the real "n" (see Buffer::Instance for details).
  int nSlices = buffer.getRawSlices(&oneRawSlice, 1);
  if (nSlices <= 0) {
    return 0;
  }
  uint8_t* m = static_cast<uint8_t*>(allocMemory(length, &pointer));
  if (nSlices == 1) {
    memcpy(m, oneRawSlice.mem_, oneRawSlice.len_);
    return pointer;
  }
  STACK_ARRAY(manyRawSlices, Buffer::RawSlice, nSlices);
  buffer.getRawSlices(manyRawSlices.begin(), nSlices);
  auto p = m;
  for (int i = 0; i < nSlices; i++) {
    memcpy(p, manyRawSlices[i].mem_, manyRawSlices[i].len_);
    p += manyRawSlices[i].len_;
  }
  return pointer;
}

inline bool Wasm::copyToPointerSize(absl::string_view s, uint64_t ptr_ptr, uint64_t size_ptr) {
  uint64_t pointer = 0;
  uint64_t size = s.size();
  void* p = nullptr;
  if (size > 0) {
    p = allocMemory(size, &pointer);
    if (!p) {
      return false;
    }
    memcpy(p, s.data(), size);
  }
  if (!wasm_vm_->setWord(ptr_ptr, Word(pointer))) {
    return false;
  }
  if (!wasm_vm_->setWord(size_ptr, Word(size))) {
    return false;
  }
  return true;
}

inline bool Wasm::copyToPointerSize(const Buffer::Instance& buffer, uint64_t start, uint64_t length,
                                    uint64_t ptr_ptr, uint64_t size_ptr) {
  uint64_t size = buffer.length();
  if (size < start + length) {
    return false;
  }
  auto nslices = buffer.getRawSlices(nullptr, 0);
  auto slices = std::make_unique<Buffer::RawSlice[]>(nslices + 10 /* pad for evbuffer overrun */);
  auto actual_slices = buffer.getRawSlices(&slices[0], nslices);
  uint64_t pointer = 0;
  char* p = static_cast<char*>(allocMemory(length, &pointer));
  auto s = start;
  auto l = length;
  if (!p) {
    return false;
  }
  for (uint64_t i = 0; i < actual_slices; i++) {
    if (slices[i].len_ <= s) {
      s -= slices[i].len_;
      continue;
    }
    auto ll = l;
    if (ll > s + slices[i].len_)
      ll = s + slices[i].len_;
    memcpy(p, static_cast<char*>(slices[i].mem_) + s, ll);
    l -= ll;
    if (l <= 0) {
      break;
    }
    s = 0;
    p += ll;
  }
  if (!wasm_vm_->setWord(ptr_ptr, Word(pointer))) {
    return false;
  }
  if (!wasm_vm_->setWord(size_ptr, Word(length))) {
    return false;
  }
  return true;
}

template <typename T> inline bool Wasm::setDatatype(uint64_t ptr, const T& t) {
  return wasm_vm_->setMemory(ptr, sizeof(T), &t);
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
