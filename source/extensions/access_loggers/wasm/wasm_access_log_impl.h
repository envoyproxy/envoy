#pragma once

#include "envoy/access_log/access_log.h"

#include "common/common/logger.h"

#include "extensions/access_loggers/well_known_names.h"
#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::PluginHandle;
using Envoy::Extensions::Common::Wasm::PluginSharedPtr;

class WasmAccessLog : public AccessLog::Instance {
public:
  WasmAccessLog(const envoy::extensions::wasm::v3::PluginConfig& proto_config,
                const PluginSharedPtr& plugin, ThreadLocal::TypedSlotPtr<PluginHandle>&& tls_slot,
                AccessLog::FilterPtr filter)
      : base_config_(
            std::make_unique<Envoy::Extensions::Common::Wasm::WasmBaseConfig>(proto_config)),
        plugin_(plugin), tls_slot_(std::move(tls_slot)), filter_(std::move(filter)) {}

  void log(const Http::RequestHeaderMap* request_headers,
           const Http::ResponseHeaderMap* response_headers,
           const Http::ResponseTrailerMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info) override {
    if (filter_ && request_headers && response_headers && response_trailers) {
      if (!filter_->evaluate(stream_info, *request_headers, *response_headers,
                             *response_trailers)) {
        return;
      }
    }

    auto handle = tls_slot_->get();
    if (handle.has_value()) {
      handle->wasm()->log(plugin_, request_headers, response_headers, response_trailers,
                          stream_info);
    }
  }

  void setTlsSlot(ThreadLocal::TypedSlotPtr<PluginHandle>&& tls_slot) {
    ASSERT(tls_slot_ == nullptr);
    tls_slot_ = std::move(tls_slot);
  }

  Envoy::Extensions::Common::Wasm::WasmBaseConfig& baseConfig() { return *base_config_; }

private:
  Envoy::Extensions::Common::Wasm::WasmBaseConfigPtr base_config_;
  PluginSharedPtr plugin_;
  ThreadLocal::TypedSlotPtr<PluginHandle> tls_slot_;
  AccessLog::FilterPtr filter_;
};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
