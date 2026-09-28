#pragma once

#include <memory>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/request_filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_filter_manager.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Single per-stream manager that owns all AI filter instances for the stream and orchestrates
// their execution across both request (decode) and response (encode) paths.
//
// Request and response pipelines are separated into dedicated classes (`RequestFilterManager` and
// `ResponseFilterManager`) owned by this class. Filters execute in forward order (0..N-1) on the
// request path and in reverse order (N-1..0) on the response path, sharing the same filter
// instances across both directions.
class FilterManager : public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  using LocalReplyFn = RequestFilterManager::LocalReplyFn;
  using OnCompleteFn = absl::AnyInvocable<void(absl::Status)>;

  explicit FilterManager(std::vector<AiFilterSharedPtr> filters);
  ~FilterManager();

  // Starts the request filter chain in forward filter order (0..N-1). Unless `always_serialize`,
  // the received body is forwarded instead of the re-serialized document.
  void startRequest(JsonWithExtBuf payload_index, BufferManager* buffer_manager,
                    Event::Dispatcher& dispatcher, StreamInfo::StreamInfo& stream_info,
                    OnCompleteFn on_complete, Http::RequestHeaderMap* request_headers = nullptr,
                    LocalReplyFn local_reply_fn = nullptr, bool always_serialize = true);

  // Starts the SSE response filter chain in reverse filter order (N-1..0).
  void startSseResponse(ExternalBufferFactory& buffer_factory, FilterChainBridge& bridge,
                        BufferManager& out_buffer_manager, OnCompleteFn on_complete,
                        ResponseFilterManager::Config config = {});

  // Starts the unary JSON response filter chain in reverse filter order (N-1..0).
  void startUnaryResponse(ExternalBufferFactory& buffer_factory, FilterChainBridge& bridge,
                          BufferManager& out_buffer_manager, OnCompleteFn on_complete);

  // Feeds response body bytes to the active response filter manager.
  void onResponseData(Buffer::Instance& data, bool end_stream);

  // Cancels all in-flight request and response coroutines on stream reset or teardown.
  void cancel();

private:
  std::vector<AiFilterSharedPtr> filters_;
  RequestFilterManagerPtr request_manager_;
  ResponseFilterManagerPtr response_manager_;
};

using FilterManagerPtr = std::unique_ptr<FilterManager>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
