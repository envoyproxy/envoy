#pragma once

#include "envoy/stats/sink.h"

#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::WasmHandle;

class WasmStatSink : public Stats::Sink {
public:
  WasmStatSink(absl::string_view root_id, Common::Wasm::WasmHandleSharedPtr singleton)
      : root_id_(root_id), singleton_(std::move(singleton)) {}

  void flush(Stats::MetricSnapshot& snapshot) override {
    singleton_->wasm()->onStatsUpdate(root_id_, snapshot);
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
  std::string root_id_;
  Common::Wasm::WasmHandleSharedPtr singleton_;
};

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
