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

// Runs the configured AI filters over an SSE response body.
//
// The response arrives as bytes and must leave as bytes, but filters see neither: between them
// sits a decode step, a chain of filter coroutines, and a re-serializing sink. The chain is built
// out of one-slot AsyncQueues, so it is a pipeline with no buffering of its own -- a filter that
// is slow to forward a frame stops the one behind it, and that back-pressure reaches the decoder
// and then the upstream source, rather than accumulating in memory.
//
// The manager is fed incrementally with onData(), which is decoupled from the pipeline by a
// pending-input buffer and a one-slot signal queue: onData() never blocks (it cannot -- it runs on
// the filter chain's stack), it parks the bytes, accounts them against the path's FilterChainBridge
// watermarks, and wakes the source coroutine, which is the only thing that awaits.
//
// Lifetime: the pipeline outlives any single call into it, because filter coroutines suspend
// mid-stream and resume later. Coroutines hold a weak reference and give up if the manager is
// gone, so cancel() and destruction are safe at any point.
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
