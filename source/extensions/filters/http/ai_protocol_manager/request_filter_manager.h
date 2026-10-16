#pragma once

#include <memory>
#include <optional>
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
#include "source/extensions/filters/http/ai_protocol_manager/upstream_candidates.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// The AiProtocolManager.filters entry a request stage runs.
struct AiFilterStage {
  std::string name;
  UpstreamCandidatesPolicy policy;
};

// A local reply that ends the request in place of writing it out.
struct SinkReply {
  Http::Code code;
  std::string body;
  // Response code details; no spaces.
  std::string details;
};

// Runs the configured AI filters over a request payload index.
//
// Owned and managed by FilterManager for the decode path. Filters execute in forward order
// (0..N-1). Once all filters propagate the request, the sink writes it out through the stream's
// BufferManager: the re-serialized document, or the received body when `always_serialize` is
// false.
class RequestFilterManager : public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  using LocalReplyFn =
      absl::AnyInvocable<void(Http::Code code, std::string body, absl::string_view details)>;
  using OnCompleteFn = absl::AnyInvocable<void(absl::Status)>;
  // Runs once, after every AI filter propagated or was bypassed and before the sink writes the
  // body, while the request headers are held. `decision` is sealed, or null when no AI filter
  // seeded one.
  using OnFiltersDoneFn = absl::AnyInvocable<std::optional<SinkReply>(
      AiRequest& request, std::shared_ptr<UpstreamCandidates> decision)>;

  // `stages` names the filters by index; `upstream_candidates_enabled` gives each filter an
  // UpstreamCandidatesEditor.
  RequestFilterManager(std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
                       BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
                       StreamInfo::StreamInfo& stream_info, OnCompleteFn on_complete,
                       Http::RequestHeaderMap* request_headers = nullptr,
                       LocalReplyFn local_reply_fn = nullptr, bool always_serialize = true,
                       LLMProtocol request_protocol = LLMProtocol::Unspecified,
                       OnFiltersDoneFn on_filters_done = nullptr,
                       std::vector<AiFilterStage> stages = {},
                       bool upstream_candidates_enabled = false);
  ~RequestFilterManager();

  // Starts the request filter pipeline and sink coroutines.
  void start();

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
