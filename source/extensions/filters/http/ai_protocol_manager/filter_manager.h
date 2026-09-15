#pragma once

#include <memory>
#include <vector>

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class FilterManager : public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  using LocalReplyFn = absl::AnyInvocable<void(Http::Code code, std::string details)>;

  FilterManager(std::vector<AiFilterPtr> filters, JsonWithExtBuf payload_index,
                BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
                StreamInfo::StreamInfo& stream_info,
                Http::RequestHeaderMap* request_headers = nullptr,
                LocalReplyFn local_reply_fn = nullptr);
  ~FilterManager();

  // Launches the filter chain. Invokes `on_complete` with the final completion status.
  //
  // Contract between caller (`AiProtocolManagerFilter`) and `FilterManager`:
  // 1. Success: when the filter chain completes and the payload is serialized and replayed,
  //    `on_complete` is invoked with `absl::OkStatus()`.
  // 2. Filter-initiated local reply: when an `AiFilter` invokes `LocalReplier`, all coroutines
  //    are cancelled, `local_reply_fn` is called with the HTTP code and details string, and
  //    `on_complete` is invoked with `absl::CancelledError`.
  // 3. FilterManager internal error: when an `AiFilter` returns a non-OK status or an internal
  //    error occurs, all coroutines are cancelled, `local_reply_fn` is called with a 502 Bad
  //    Gateway local reply, and `on_complete` is invoked with that error status.
  // 4. Cancellation: when `cancel()` is called, all coroutines are cancelled and neither
  //    `local_reply_fn` nor `on_complete` is invoked.
  void start(absl::AnyInvocable<void(absl::Status)> on_complete);

  // Cancels all in-flight coroutines and cleans up state on stream reset.
  // It is safe to destruct the FilterManager immediately after calling cancel().
  void cancel();

private:
  struct AsyncState;

  void launchFilters();
  void launchSink();

  std::vector<AiFilterPtr> filters_;
  JsonWithExtBuf payload_index_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  std::shared_ptr<AsyncState> async_state_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
