#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/logger.h"

#include "extensions/common/wasm/wasm.h"
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

bool Buffer::copyTo(WasmBase* wasm, size_t start, size_t length, uint64_t ptr_ptr,
                    uint64_t size_ptr) const {
  if (start + length > data_.size()) {
    return false;
  }
  absl::string_view s = data_.substr(start, length);
  return wasm->copyToPointerSize(s, ptr_ptr, size_ptr);
}

Wasm* Context::wasm() const { return static_cast<Wasm*>(wasm_); }

void Context::error(absl::string_view message) { throw WasmException(std::string(message)); }

void Context::initializeRoot(WasmBase* wasm, PluginBaseSharedPtr plugin) {
  ContextBase::initializeRoot(wasm, plugin);
  root_local_info_ = &std::static_pointer_cast<Plugin>(plugin)->local_info_;
}

uint64_t Context::getCurrentTimeNanoseconds() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             wasm()->time_source_.systemTime().time_since_epoch())
      .count();
}

WasmResult Context::log(uint64_t level, absl::string_view message) {
  switch (static_cast<proxy_wasm::LogLevel>(level)) {
  case proxy_wasm::LogLevel::trace:
    ENVOY_LOG(trace, "wasm log{}: {}", log_prefix(), message);
    break;
  case proxy_wasm::LogLevel::debug:
    ENVOY_LOG(debug, "wasm log{}: {}", log_prefix(), message);
    break;
  case proxy_wasm::LogLevel::info:
    ENVOY_LOG(info, "wasm log{}: {}", log_prefix(), message);
    break;
  case proxy_wasm::LogLevel::warn:
    ENVOY_LOG(warn, "wasm log{}: {}", log_prefix(), message);
    break;
  case proxy_wasm::LogLevel::error:
    ENVOY_LOG(error, "wasm log{}: {}", log_prefix(), message);
    break;
  case proxy_wasm::LogLevel::critical:
    ENVOY_LOG(critical, "wasm log{}: {}", log_prefix(), message);
    break;
  default:
    return WasmResult::BadArgument;
  }
  return WasmResult::Ok;
}

WasmResult Context::getProperty(absl::string_view path, std::string* result) {
  if (path.size() == 15 && !memcmp(reinterpret_cast<const void*>(&*path.begin()),
                                   reinterpret_cast<const void*>("plugin_root_id\0"), 15)) {
    *result = std::string(root_id());
    return WasmResult::Ok;
  }
  return WasmResult::Unimplemented;
}

WasmResult Context::defineMetric(MetricType type, absl::string_view name, uint32_t* metric_id_ptr) {
  auto stat_name = wasm()->stat_name_set_->add(name);
  switch (type) {
  case MetricType::Counter: {
    auto id = wasm_->nextCounterMetricId();
    auto c = &wasm()->scope_->counterFromStatName(stat_name);
    wasm()->counters_.emplace(id, c);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  }
  case MetricType::Gauge: {
    auto id = wasm()->nextGaugeMetricId();
    auto g = &wasm()->scope_->gaugeFromStatName(stat_name, Stats::Gauge::ImportMode::Accumulate);
    wasm()->gauges_.emplace(id, g);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  }
  case MetricType::Histogram: {
    auto id = wasm()->nextHistogramMetricId();
    auto h = &wasm()->scope_->histogramFromStatName(stat_name, Stats::Histogram::Unit::Unspecified);
    wasm()->histograms_.emplace(id, h);
    *metric_id_ptr = id;
    return WasmResult::Ok;
  }
  default:
    return WasmResult::BadArgument;
  }
}

WasmResult Context::incrementMetric(uint32_t metric_id, int64_t offset) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm()->counters_.find(metric_id);
    if (it != wasm()->counters_.end()) {
      if (offset > 0) {
        it->second->add(offset);
        return WasmResult::Ok;
      } else {
        return WasmResult::BadArgument;
      }
      return WasmResult::NotFound;
    }
  } else if (type == MetricType::Gauge) {
    auto it = wasm()->gauges_.find(metric_id);
    if (it != wasm()->gauges_.end()) {
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
    auto it = wasm()->counters_.find(metric_id);
    if (it != wasm()->counters_.end()) {
      it->second->add(value);
      return WasmResult::Ok;
    }
  } else if (type == MetricType::Gauge) {
    auto it = wasm()->gauges_.find(metric_id);
    if (it != wasm()->gauges_.end()) {
      it->second->set(value);
      return WasmResult::Ok;
    }
  } else if (type == MetricType::Histogram) {
    auto it = wasm()->histograms_.find(metric_id);
    if (it != wasm()->histograms_.end()) {
      it->second->recordValue(value);
      return WasmResult::Ok;
    }
  }
  return WasmResult::NotFound;
}

WasmResult Context::getMetric(uint32_t metric_id, uint64_t* result_uint64_ptr) {
  auto type = static_cast<MetricType>(metric_id & Wasm::kMetricTypeMask);
  if (type == MetricType::Counter) {
    auto it = wasm()->counters_.find(metric_id);
    if (it != wasm()->counters_.end()) {
      *result_uint64_ptr = it->second->value();
      return WasmResult::Ok;
    }
    return WasmResult::NotFound;
  } else if (type == MetricType::Gauge) {
    auto it = wasm()->gauges_.find(metric_id);
    if (it != wasm()->gauges_.end()) {
      *result_uint64_ptr = it->second->value();
      return WasmResult::Ok;
    }
    return WasmResult::NotFound;
  }
  return WasmResult::BadArgument;
}

const BufferInterface* Context::getBuffer(WasmBufferType type) {
  switch (type) {
  case WasmBufferType::VmConfiguration:
    return buffer_.set(wasm()->vm_configuration());
  case WasmBufferType::PluginConfiguration:
    if (plugin_)
      return buffer_.set(plugin_->plugin_configuration_);
    break;
  default:
    break;
  }
  return nullptr;
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
