#pragma once

#include <memory>

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/stats/sink.h"

#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::PluginHandleSharedPtr;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;

class WasmStatSink : public Stats::Sink {
public:
  WasmStatSink(const envoy::extensions::wasm::v3::PluginConfig& proto_config,
               const PluginSharedPtr& plugin, PluginHandleSharedPtr singleton)
      : base_config_(
            std::make_unique<Envoy::Extensions::Common::Wasm::WasmBaseConfig>(proto_config)),
        plugin_(plugin), singleton_(singleton) {}

  void flush(Stats::MetricSnapshot& snapshot) override {
    singleton_->wasm()->onStatsUpdate(plugin_, snapshot);
  }

  void setSingleton(PluginHandleSharedPtr singleton) {
    ASSERT(singleton != nullptr);
    singleton_ = singleton;
  }

  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override {
    (void)histogram;
    (void)value;
  }

  Envoy::Extensions::Common::Wasm::WasmBaseConfig& baseConfig() { return *base_config_; }

private:
  Envoy::Extensions::Common::Wasm::WasmBaseConfigPtr base_config_;
  PluginSharedPtr plugin_;
  PluginHandleSharedPtr singleton_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
