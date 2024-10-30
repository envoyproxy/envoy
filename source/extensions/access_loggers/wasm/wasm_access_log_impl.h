#pragma once

#include "envoy/access_log/access_log.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

using Common::Wasm::PluginHandleSharedPtrThreadLocal;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;

class WasmAccessLog : public AccessLog::Instance {
public:
  WasmAccessLog(Extensions::Common::Wasm::PluginConfigPtr plugin_config,
                AccessLog::FilterPtr filter)
      : plugin_config_(std::move(plugin_config)), filter_(std::move(filter)) {}

  void log(const Formatter::HttpFormatterContext& log_context,
           const StreamInfo::StreamInfo& stream_info) override {
    if (filter_) {
      if (!filter_->evaluate(log_context, stream_info)) {
        return;
      }
    }

    if (Common::Wasm::Wasm* wasm = plugin_config_->wasm(); wasm != nullptr) {
      wasm->log(plugin_config_->plugin(), log_context, stream_info);
    }
  }

private:
  Common::Wasm::PluginConfigPtr plugin_config_;
  AccessLog::FilterPtr filter_;
};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
