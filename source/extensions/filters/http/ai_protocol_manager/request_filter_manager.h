#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
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

// Runs the configured AI filters over a request payload index.
//
// Owned and managed by FilterManager for the decode path. Filters execute in forward order
// (0..N-1). Once all filters propagate the request index, the sink serializes the resulting
// payload index back into the stream's BufferManager.
class RequestFilterManager : public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  using LocalReplyFn = absl::AnyInvocable<void(Http::Code code, std::string details)>;
  using OnCompleteFn = absl::AnyInvocable<void(absl::Status)>;

  RequestFilterManager(std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
                       BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
                       StreamInfo::StreamInfo& stream_info, OnCompleteFn on_complete,
                       Http::RequestHeaderMap* request_headers = nullptr,
                       LocalReplyFn local_reply_fn = nullptr);
  ~RequestFilterManager();

  // Cancels all in-flight request coroutines and cleans up state on stream reset.
  void cancel();

  class AsyncState;

private:
  std::shared_ptr<AsyncState> async_state_;
};

using RequestFilterManagerPtr = std::unique_ptr<RequestFilterManager>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
