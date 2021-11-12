#pragma once

#include <memory>

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/stats/sink.h"

#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtr;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;

class WasmStatSink : public Stats::Sink {
public:
  WasmStatSink(const PluginSharedPtr& plugin, PluginHandleSharedPtr singleton)
      : plugin_(plugin), singleton_(singleton) {}

  void flush(Stats::MetricSnapshot& snapshot) override {
    singleton_->wasmHandle()->wasm()->onStatsUpdate(plugin_, snapshot);
  }

  void setSingleton(PluginHandleSharedPtr singleton) {
    ASSERT(singleton != nullptr);
    singleton_ = singleton;
  }

  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override {
    (void)histogram;
    (void)value;
  }

private:
  PluginSharedPtr plugin_;
  PluginHandleSharedPtr singleton_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
