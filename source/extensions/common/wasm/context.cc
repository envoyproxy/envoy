#include <stdio.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/wasm/v2/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/common/wasm/wasm.h"
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

Context::Context() : root_context_(this) {}

Context::Context(Wasm* wasm) : wasm_(wasm), root_context_(this) { wasm_->contexts_[id_] = this; }

Context::Context(Wasm* wasm, absl::string_view root_id, PluginSharedPtr plugin)
    : wasm_(wasm), id_(wasm->allocContextId()), root_context_(this), plugin_(plugin) {
  RELEASE_ASSERT(root_id == plugin->root_id_, "");
  wasm_->contexts_[id_] = this;
}

Context::Context(Wasm* wasm, uint32_t root_context_id, PluginSharedPtr plugin)
    : wasm_(wasm), id_(wasm->allocContextId()), root_context_id_(root_context_id), plugin_(plugin) {
  wasm_->contexts_[id_] = this;
  root_context_ = wasm_->contexts_[root_context_id_];
}

WasmVm* Context::wasmVm() const { return wasm_->wasm_vm(); }

uint64_t Context::getCurrentTimeNanoseconds() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             wasm_->time_source_.systemTime().time_since_epoch())
      .count();
}

void Context::scriptLog(spdlog::level::level_enum level, absl::string_view message) {
  switch (level) {
  case spdlog::level::trace:
    ENVOY_LOG(trace, "wasm log{}: {}", log_prefix(), message);
    return;
  case spdlog::level::debug:
    ENVOY_LOG(debug, "wasm log{}: {}", log_prefix(), message);
    return;
  case spdlog::level::info:
    ENVOY_LOG(info, "wasm log{}: {}", log_prefix(), message);
    return;
  case spdlog::level::warn:
    ENVOY_LOG(warn, "wasm log{}: {}", log_prefix(), message);
    return;
  case spdlog::level::err:
    ENVOY_LOG(error, "wasm log{}: {}", log_prefix(), message);
    return;
  case spdlog::level::critical:
    ENVOY_LOG(critical, "wasm log{}: {}", log_prefix(), message);
    return;
  case spdlog::level::off:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

//
// Calls into the WASM code.
//
bool Context::onStart(absl::string_view vm_configuration, PluginSharedPtr plugin) {
  bool result = 0;
  if (wasm_->on_start_) {
    configuration_ = vm_configuration;
    plugin_ = plugin;
    result = wasm_->on_start_(this, id_, static_cast<uint32_t>(vm_configuration.size())).u64_ != 0;
    plugin_.reset();
    configuration_ = "";
  }
  return result;
}

bool Context::validateConfiguration(absl::string_view configuration, PluginSharedPtr plugin) {
  if (!wasm_->validate_configuration_) {
    return true;
  }
  configuration_ = configuration;
  plugin_ = plugin;
  auto result =
      wasm_->validate_configuration_(this, id_, static_cast<uint32_t>(configuration.size())).u64_ !=
      0;
  plugin_.reset();
  configuration_ = "";
  return result;
}

bool Context::onConfigure(absl::string_view plugin_configuration, PluginSharedPtr plugin) {
  if (!wasm_->on_configure_) {
    return true;
  }
  configuration_ = plugin_configuration;
  plugin_ = plugin;
  auto result =
      wasm_->on_configure_(this, id_, static_cast<uint32_t>(plugin_configuration.size())).u64_ != 0;
  plugin_.reset();
  configuration_ = "";
  return result;
}

absl::string_view Context::getConfiguration() { return configuration_; }

void Context::onCreate(uint32_t root_context_id) {
  if (wasm_->on_create_) {
    wasm_->on_create_(this, id_, root_context_id);
  }
}

WasmResult Context::defineMetric(MetricType type, absl::string_view name, uint32_t* metric_id_ptr) {
  auto stat_name = wasm_->stat_name_set_->getDynamic(name);
  if (type == MetricType::Counter) {
    auto id = wasm_->nextCounterMetricId();
    auto c = &wasm_->scope_->counterFromStatName(stat_name);
    wasm_->counters_.emplace(id, c);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  } else if (type == MetricType::Gauge) {
    auto id = wasm_->nextGaugeMetricId();
    auto g = &wasm_->scope_->gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::Accumulate);
    wasm_->gauges_.emplace(id, g);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  } else if (type == MetricType::Histogram) {
    auto id = wasm_->nextHistogramMetricId();
    auto h = &wasm_->scope_->histogramFromStatName(stat_name, Stats::Histogram::Unit::Unspecified);
    wasm_->histograms_.emplace(id, h);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  }
  return WasmResult::BadArgument;
}

WasmResult Context::incrementMetric(uint32_t metric_id, int64_t offset) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm_->counters_.find(metric_id);
    if (it != wasm_->counters_.end()) {
      if (offset > 0) {
        it->second->add(offset);
        return WasmResult::Ok;
      } else {
        return WasmResult::BadArgument;
      }
      return WasmResult::NotFound;
    }
  } else if (type == MetricType::Gauge) {
    auto it = wasm_->gauges_.find(metric_id);
    if (it != wasm_->gauges_.end()) {
      if (offset > 0) {
        it->second->add(offset);
        return WasmResult::Ok;
      } else {
        it->second->sub(-offset);
        return WasmResult::Ok;
      }
    }
    return WasmResult::NotFound;
  }
  return WasmResult::BadArgument;
}

WasmResult Context::recordMetric(uint32_t metric_id, uint64_t value) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm_->counters_.find(metric_id);
    if (it != wasm_->counters_.end()) {
      it->second->add(value);
      return WasmResult::Ok;
    }
  } else if (type == MetricType::Gauge) {
    auto it = wasm_->gauges_.find(metric_id);
    if (it != wasm_->gauges_.end()) {
      it->second->set(value);
      return WasmResult::Ok;
    }
  } else if (type == MetricType::Histogram) {
    auto it = wasm_->histograms_.find(metric_id);
    if (it != wasm_->histograms_.end()) {
      it->second->recordValue(value);
      return WasmResult::Ok;
    }
  }
  return WasmResult::NotFound;
}

WasmResult Context::getMetric(uint32_t metric_id, uint64_t* result_uint64_ptr) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm_->counters_.find(metric_id);
    if (it != wasm_->counters_.end()) {
      *result_uint64_ptr = it->second->value();
      return WasmResult::Ok;
    }
    return WasmResult::NotFound;
  } else if (type == MetricType::Gauge) {
    auto it = wasm_->gauges_.find(metric_id);
    if (it != wasm_->gauges_.end()) {
      *result_uint64_ptr = it->second->value();
      return WasmResult::Ok;
    }
    return WasmResult::NotFound;
  }
  return WasmResult::BadArgument;
}

std::string Plugin::makeLogPrefix() const {
  std::string prefix;
  if (!name_.empty()) {
    prefix = prefix + " " + name_;
  }
  if (!root_id_.empty()) {
    prefix = prefix + " " + std::string(root_id_);
  }
  if (vm_id_.empty()) {
    prefix = prefix + " " + std::string(vm_id_);
  }
  return prefix;
}

Context::~Context() {
  // Do not remove vm or root contexts which have the same lifetime as wasm_.
  if (root_context_id_) {
    wasm_->contexts_.erase(id_);
  }
}

bool Context::onDone() {
  if (wasm_->on_done_) {
    return wasm_->on_done_(this, id_).u64_ != 0;
  }
  return true;
}

void Context::onLog() {
  if (wasm_->on_log_) {
    wasm_->on_log_(this, id_);
  }
}

void Context::onDelete() {
  if (wasm_->on_delete_) {
    wasm_->on_delete_(this, id_);
  }
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
