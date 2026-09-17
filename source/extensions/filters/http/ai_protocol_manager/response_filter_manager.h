#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_event_codec.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Runs the configured AI filters over a response body.
//
// Owned and managed by FilterManager for the encode path. Filters execute in reverse order
// relative to the request path. Incoming response bytes fed via onData() are buffered and
// accounted against the stream's FilterChainBridge watermarks, decoded into response items by the
// active response mode (e.g. SSE frames), passed through the filter pipeline, and re-serialized
// into the output BufferManager.
//
// Lifetime: owns a shared_ptr<AsyncState> so coroutines can safely suspend across asynchronous
// operations; cancel() and destruction safely tear down the pipeline at any point.
class ResponseFilterManager {
public:
  // Invoked once when the response has been fully processed and written, or has failed. An error
  // means the response is no longer trustworthy and the stream should be torn down.
  using OnCompleteFn = absl::AnyInvocable<void(absl::Status)>;

  struct Config {
    SseEventDecoder::Config sse{};
  };

  ResponseFilterManager(std::vector<AiFilterSharedPtr> filters,
                        ExternalBufferFactory& buffer_factory, FilterChainBridge& bridge,
                        BufferManager& out_buffer_manager, OnCompleteFn on_complete, Config config);
  ~ResponseFilterManager();

  // Feeds response body bytes. `data` is drained. Must be called with end_stream true exactly
  // once, as the last call.
  void onData(Buffer::Instance& data, bool end_stream);

  // Tears the pipeline down without completing. Idempotent.
  void cancel();

  // Pipeline internals, kept out of this header.
  class AsyncState;

private:
  std::shared_ptr<AsyncState> async_state_;
};

using ResponseFilterManagerPtr = std::unique_ptr<ResponseFilterManager>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
