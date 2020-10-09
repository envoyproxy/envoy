#pragma once

#include "envoy/access_log/access_log.h"

#include "common/common/logger.h"

#include "extensions/access_loggers/well_known_names.h"
#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Wasm {

using Envoy::Extensions::Common::Wasm::WasmHandle;

class WasmAccessLog : public AccessLog::Instance {
public:
  WasmAccessLog(absl::string_view root_id, ThreadLocal::SlotPtr tls_slot,
                AccessLog::FilterPtr filter)
      : root_id_(root_id), tls_slot_(std::move(tls_slot)), filter_(std::move(filter)) {}
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

    if (tls_slot_->get()) {
      tls_slot_->getTyped<WasmHandle>().wasm()->log(root_id_, request_headers, response_headers,
                                                    response_trailers, stream_info);
    }
  }

  void setTlsSlot(ThreadLocal::SlotPtr tls_slot) {
    ASSERT(tls_slot_ == nullptr);
    tls_slot_ = std::move(tls_slot);
  }

private:
  std::string root_id_;
  ThreadLocal::SlotPtr tls_slot_;
  AccessLog::FilterPtr filter_;
};

} // namespace Wasm
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
