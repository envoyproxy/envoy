#pragma once

#include <atomic>
#include <map>
#include <memory>

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"

#include "envoy/config/wasm/v2/wasm.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

#include "extensions/common/wasm/proxy_wasm_common.h"

class Wasm;
class WasmVm;

using Pairs = std::vector<std::pair<absl::string_view, absl::string_view>>;
using PairsWithStringValues = std::vector<std::pair<absl::string_view, std::string>>;

// Plugin contains the information for a filter/service.
struct Plugin {
  Plugin(absl::string_view name, absl::string_view root_id, absl::string_view vm_id,
         envoy::api::v2::core::TrafficDirection direction, const LocalInfo::LocalInfo& local_info,
         const envoy::api::v2::core::Metadata* listener_metadata)
      : name_(std::string(name)), root_id_(std::string(root_id)), vm_id_(std::string(vm_id)),
        direction_(direction), local_info_(local_info), listener_metadata_(listener_metadata),
        log_prefix_(makeLogPrefix()) {}

  std::string makeLogPrefix() const;

  const std::string name_;
  const std::string root_id_;
  const std::string vm_id_;
  envoy::api::v2::core::TrafficDirection direction_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::api::v2::core::Metadata* listener_metadata_;

  std::string log_prefix_;
};
using PluginSharedPtr = std::shared_ptr<Plugin>;

// A context which will be the target of callbacks for a particular session
// e.g. a handler of a stream.
class Context : public Logger::Loggable<Logger::Id::wasm>,
                public std::enable_shared_from_this<Context> {
public:
  Context();                                                              // Testing.
  Context(Wasm* wasm);                                                    // Vm Context.
  Context(Wasm* wasm, absl::string_view root_id, PluginSharedPtr plugin); // Root Context.
  Context(Wasm* wasm, uint32_t root_context_id, PluginSharedPtr plugin);  // Stream context.
  virtual ~Context();

  Wasm* wasm() const { return wasm_; }
  uint32_t id() const { return id_; }
  bool isVmContext() { return id_ == 0; }
  bool isRootContext() { return root_context_id_ == 0; }
  Context* root_context() { return root_context_; }

  absl::string_view root_id() const { return plugin_->root_id_; }
  absl::string_view log_prefix() const { return plugin_->log_prefix_; }

  WasmVm* wasmVm() const;

  //
  // VM level downcalls into the WASM code on Context(id == 0).
  //
  virtual bool validateConfiguration(absl::string_view configuration, PluginSharedPtr plugin);
  virtual bool onStart(absl::string_view vm_configuration, PluginSharedPtr plugin);
  virtual bool onConfigure(absl::string_view plugin_configuration, PluginSharedPtr plugin);

  //
  // Stream downcalls on Context(id > 0).
  //
  // General stream downcall on a new stream.
  virtual void onCreate(uint32_t root_context_id);
  // General stream downcall when the stream/vm has ended.
  virtual bool onDone();
  // General stream downcall for logging. Occurs after onDone().
  virtual void onLog();
  // General stream downcall when no further stream calls will occur.
  virtual void onDelete();

  //
  // General Callbacks.
  //
  virtual void scriptLog(spdlog::level::level_enum level, absl::string_view message);
  virtual uint64_t getCurrentTimeNanoseconds();
  virtual absl::string_view getConfiguration();

  // Stats/Metrics
  enum class MetricType : uint32_t {
    Counter = 0,
    Gauge = 1,
    Histogram = 2,
    Max = 2,
  };
  virtual WasmResult defineMetric(MetricType type, absl::string_view name, uint32_t* metric_id_ptr);
  virtual WasmResult incrementMetric(uint32_t metric_id, int64_t offset);
  virtual WasmResult recordMetric(uint32_t metric_id, uint64_t value);
  virtual WasmResult getMetric(uint32_t metric_id, uint64_t* value_ptr);

protected:
  friend class Wasm;

  Wasm* wasm_{nullptr};
  uint32_t id_{0};
  uint32_t root_context_id_{0};    // 0 for roots and the general context.
  Context* root_context_{nullptr}; // set in all contexts.
  PluginSharedPtr plugin_;
  bool in_vm_context_created_ = false;
  bool destroyed_ = false;

  // General state.
  // NB: this are only available (non-nullptr) during the calls requiring it (e.g. onConfigure()).
  absl::string_view configuration_;
};
using ContextSharedPtr = std::shared_ptr<Context>;

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
