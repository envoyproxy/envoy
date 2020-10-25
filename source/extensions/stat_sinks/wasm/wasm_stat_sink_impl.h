#pragma once

#include "envoy/stats/sink.h"

#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::WasmHandle;

class WasmStatSink : public Stats::Sink {
public:
  WasmStatSink(const PluginSharedPtr& plugin, Common::Wasm::WasmHandleSharedPtr singleton)
      : plugin_(plugin), singleton_(std::move(singleton)) {}

  void flush(Stats::MetricSnapshot& snapshot) override {
    singleton_->wasm()->onStatsUpdate(plugin_, snapshot);
  }

  void setSingleton(Common::Wasm::WasmHandleSharedPtr singleton) {
    ASSERT(singleton != nullptr);
    singleton_ = std::move(singleton);
  }

  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override {
    (void)histogram;
    (void)value;
  }

private:
  Common::Wasm::PluginSharedPtr plugin_;
  Common::Wasm::WasmHandleSharedPtr singleton_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
