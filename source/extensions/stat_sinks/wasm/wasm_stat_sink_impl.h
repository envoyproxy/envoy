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
  WasmStatSink(Common::Wasm::PluginConfigPtr plugin_config)
      : plugin_config_(std::move(plugin_config)) {}

  void flush(Stats::MetricSnapshot& snapshot) override {
    if (Common::Wasm::Wasm* wasm = plugin_config_->wasm(); wasm != nullptr) {
      wasm->onStatsUpdate(plugin_config_->plugin(), snapshot);
    }
  }

  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override {
    (void)histogram;
    (void)value;
  }

private:
  Common::Wasm::PluginConfigPtr plugin_config_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
