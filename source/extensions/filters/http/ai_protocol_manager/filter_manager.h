#pragma once

#include <memory>
#include <vector>

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
                StreamInfo::StreamInfo& stream_info, LocalReplyFn local_reply_fn = nullptr);
  ~FilterManager();

  // Launches the filter chain. Invokes on_complete with the final completion status.
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
