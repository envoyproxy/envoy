#pragma once

#include <memory>

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/stats/sink.h"

#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::PluginHandleManagerSharedPtr;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;
using Envoy::Extensions::Common::Wasm::Wasm;

class WasmStatSink : public Stats::Sink {
public:
  WasmStatSink(const PluginSharedPtr& plugin, PluginHandleManagerSharedPtr singleton)
      : plugin_(plugin), singleton_(singleton) {}

  void flush(Stats::MetricSnapshot& snapshot) override {
    Wasm* wasm = nullptr;
    auto handle = singleton_->handle();
    if (handle->wasmHandle()) {
      wasm = handle->wasmHandle()->wasm().get();
      if (wasm->isFailed()) {
        // Try to restart.
        if (singleton_->tryRestartPlugin()) {
          handle = singleton_->handle();
          wasm = handle->wasmHandle()->wasm().get();
        }
      }
    }
    if (wasm && !wasm->isFailed()) {
      wasm->onStatsUpdate(plugin_, snapshot);
    }
  }

  void setSingleton(PluginHandleManagerSharedPtr singleton) {
    ASSERT(singleton != nullptr);
    singleton_ = singleton;
  }

  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override {
    (void)histogram;
    (void)value;
  }

private:
  PluginSharedPtr plugin_;
  PluginHandleManagerSharedPtr singleton_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
