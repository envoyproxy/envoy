#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

#include "include/proxy-wasm/context.h"
#include "include/proxy-wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class Wasm;

#include "proxy_wasm_common.h"

using proxy_wasm::BufferInterface;
using proxy_wasm::MetricType;
using proxy_wasm::Pairs;
using proxy_wasm::PairsWithStringValues;
using proxy_wasm::PluginBase;
using proxy_wasm::WasmBase;
using proxy_wasm::WasmBufferType;
using proxy_wasm::WasmHandle;
using proxy_wasm::WasmResult;
using proxy_wasm::WasmVm;

using PluginBaseSharedPtr = std::shared_ptr<PluginBase>;

class Buffer : public proxy_wasm::BufferInterface {
public:
  Buffer() {}

  // Add additional wrapped types here.
  Buffer* set(absl::string_view data) {
    data_ = data;
    return this;
  }

  size_t size() const override { return data_.size(); }
  bool copyTo(WasmBase* wasm, size_t start, size_t length, uint64_t ptr_ptr,
              uint64_t size_ptr) const override;

private:
  absl::string_view data_;
};

// Plugin contains the information for a filter/service.
struct Plugin : public PluginBase {
  Plugin(absl::string_view name, absl::string_view root_id, absl::string_view vm_id,
         absl::string_view plugin_configuration,
         const envoy::config::core::v3::TrafficDirection& direction,
         const LocalInfo::LocalInfo& local_info,
         const envoy::config::core::v3::Metadata* listener_metadata)
      : PluginBase(name, root_id, vm_id, plugin_configuration), direction_(direction),
        local_info_(local_info), listener_metadata_(listener_metadata) {}

  // Information specific to HTTP Filters.
  // TODO: consider using a varient record or filter-type specific sub-objects via unique_ptr.
  const envoy::config::core::v3::TrafficDirection direction_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::config::core::v3::Metadata* listener_metadata_;
};
using PluginSharedPtr = std::shared_ptr<Plugin>;

// A context which will be the target of callbacks for a particular session
// e.g. a handler of a stream.
class Context : public proxy_wasm::ContextBase,
                Logger::Loggable<Logger::Id::wasm>,
                public std::enable_shared_from_this<Context> {
public:
  Context();                                                                     // Testing.
  Context(WasmBase* wasm) : ContextBase(wasm) {}                                 // Vm Context.
  Context(WasmBase* wasm, PluginSharedPtr plugin) : ContextBase(wasm, plugin) {} // Root Context.
  Context(WasmBase* wasm, uint32_t root_context_id, PluginSharedPtr plugin)
      : ContextBase(wasm, root_context_id, plugin) {} // Stream context.
  virtual ~Context() {}

  Wasm* wasm() const;

  // proxy_wasm::ContextBase
  void error(absl::string_view message) override;

  /**
   * General Callbacks.
   */
  // proxy_wasm::ContextBase
  WasmResult log(uint64_t level, absl::string_view message) override;
  uint64_t getCurrentTimeNanoseconds() override;

  // Buffer
  // proxy_wasm::ContextBase
  const BufferInterface* getBuffer(WasmBufferType type) override;

  // Properties
  // proxy_wasm::ContextBase
  WasmResult getProperty(absl::string_view path, std::string* result) override;

  // Metrics
  // proxy_wasm::ContextBase
  WasmResult defineMetric(MetricType type, absl::string_view name,
                          uint32_t* metric_id_ptr) override;
  WasmResult incrementMetric(uint32_t metric_id, int64_t offset) override;
  WasmResult recordMetric(uint32_t metric_id, uint64_t value) override;
  WasmResult getMetric(uint32_t metric_id, uint64_t* value_ptr) override;

protected:
  void initializeRoot(WasmBase* wasm, PluginBaseSharedPtr plugin) override;

  const LocalInfo::LocalInfo* root_local_info_{nullptr}; // set only for root_context.

  // Temporary state used for and valid only during calls into the VM.
  Buffer buffer_;
};
using ContextSharedPtr = std::shared_ptr<Context>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
