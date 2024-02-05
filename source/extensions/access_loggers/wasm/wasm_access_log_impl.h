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
  WasmAccessLog(const PluginSharedPtr& plugin,
                ThreadLocal::TypedSlotPtr<PluginHandleSharedPtrThreadLocal>&& tls_slot,
                AccessLog::FilterPtr filter)
      : plugin_(plugin), tls_slot_(std::move(tls_slot)), filter_(std::move(filter)) {}

  void log(const Formatter::HttpFormatterContext& log_context,
           const StreamInfo::StreamInfo& stream_info) override {
    if (filter_) {
      if (!filter_->evaluate(log_context, stream_info)) {
        return;
      }
    }

    if (tls_slot_ != nullptr) {
      if (auto handle = tls_slot_->get()->handle(); handle != nullptr) {
        if (handle->wasmHandle()) {
          handle->wasmHandle()->wasm()->log(plugin_, log_context, stream_info);
        }
      }
    }
  }

  void setTlsSlot(ThreadLocal::TypedSlotPtr<PluginHandleSharedPtrThreadLocal>&& tls_slot) {
    ASSERT(tls_slot_ == nullptr);
    tls_slot_ = std::move(tls_slot);
  }

private:
  PluginSharedPtr plugin_;
  ThreadLocal::TypedSlotPtr<PluginHandleSharedPtrThreadLocal> tls_slot_;
  AccessLog::FilterPtr filter_;
};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
