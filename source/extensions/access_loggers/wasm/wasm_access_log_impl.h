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

  void log(const Http::RequestHeaderMap* request_headers,
           const Http::ResponseHeaderMap* response_headers,
           const Http::ResponseTrailerMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info,
           AccessLog::AccessLogType access_log_type) override {
    if (filter_ && request_headers && response_headers && response_trailers) {
      if (!filter_->evaluate(stream_info, *request_headers, *response_headers, *response_trailers,
                             access_log_type)) {
        return;
      }
    }

    if (!tls_slot_) {
      return;
    }
    auto handle = tls_slot_->get()->handle();
    if (!handle) {
      return;
    }
    if (handle->wasmHandle()) {
      handle->wasmHandle()->wasm()->log(plugin_, request_headers, response_headers,
                                        response_trailers, stream_info, access_log_type);
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
