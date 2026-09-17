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

// Helper base class for AI filters that finish all request processing inline on the calling thread
// without suspending or awaiting asynchronous operations (such as gRPC/HTTP calls, timers, or
// external buffer reads).
//
// Subclasses implement decodeSync(), which receives the in-memory AiRequest and a LocalReplier
// callback. This base class manages receiving and propagating the request:
// - If decodeSync() invokes reply_locally, the filter chain stops and sends the local reply.
// - If decodeSync() returns a non-OK absl::Status without invoking reply_locally, the filter chain
//   fails with a 502 Bad Gateway local reply.
// - If decodeSync() returns absl::OkStatus() without invoking reply_locally, the request is
//   automatically forwarded to the next filter in the chain.
//
// DO NOT use this base class if the filter needs to co_await any asynchronous operation;
// implement HttpFilters::AiProtocolManager::AiFilter directly instead.
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
