#pragma once

#include <string>
#include <utility>

#include "envoy/common/pure.h"
#include "envoy/http/codes.h"

#include "source/common/coroutine/status_macros.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Common {

// Adapter for AI filters whose decodeSync() hook completes inline without
// blocking or requiring asynchronous completion.
//
// decodeSync() inspects or mutates the request's in-memory JSON index.
// Accessing that index does not implicitly load externally buffered payloads.
//
// Request receipt and propagation remain coroutine-based and may suspend.
// Later serialization may also perform asynchronous external-buffer reads.
//
// The hook must not retain the request or local-reply callback for later use.
// After issuing a local reply, it should return without further stream access.
// Implement AiFilter directly when the filter's own processing must wait for
// an HTTP/gRPC call, timer, or asynchronous payload access.
class SyncAiFilter : public HttpFilters::AiProtocolManager::AiFilter {
public:
  virtual absl::Status decodeSync(HttpFilters::AiProtocolManager::AiRequest& request,
                                  HttpFilters::AiProtocolManager::LocalReplier reply_locally) PURE;

private:
  Coroutine::Task<absl::Status>
  decode(HttpFilters::AiProtocolManager::AiRequestReceiver receive_request,
         HttpFilters::AiProtocolManager::AiRequestPropagator propagate_request,
         HttpFilters::AiProtocolManager::LocalReplier reply_locally) final {
    ASSIGN_OR_CO_RETURN(HttpFilters::AiProtocolManager::AiRequestPtr request,
                        co_await std::move(receive_request)());
    bool replied_locally = false;
    HttpFilters::AiProtocolManager::LocalReplier tracked_replier =
        [&reply_locally, &replied_locally](Http::Code code, std::string details) {
          replied_locally = true;
          std::move(reply_locally)(code, std::move(details));
        };
    absl::Status status = decodeSync(*request, std::move(tracked_replier));
    if (replied_locally) {
      co_return absl::OkStatus();
    }
    if (!status.ok()) {
      co_return status;
    }
    co_return co_await std::move(propagate_request)(std::move(request));
  }
};

} // namespace Common
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
