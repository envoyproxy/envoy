#include <algorithm>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Test-only access to the retained buffer (friend of the handler). Retention
// is gated before every copy, so checking at call boundaries observes the
// hard-cap invariant completely.
class SseResponseHandlerPeer {
public:
  static uint64_t bufferedBytes(const SseResponseHandler& handler) {
    return handler.buffer_.length();
  }
};

namespace {

// Fuzzes the SSE response handler over arbitrary bytes and arbitrary frame
// fragmentation, asserting the two properties the implementation guarantees:
// retained extraction state never exceeds max_event_size, and the extraction
// result is independent of how the byte stream is sliced into frames.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  static constexpr size_t MaxInputSize = 128 * 1024;
  if (len > MaxInputSize) {
    return;
  }

  FuzzedDataProvider provider(buf, len);
  const ApiFormat format = provider.PickValueInArray(
      {ApiFormat::Unknown, ApiFormat::OpenAi, ApiFormat::Anthropic, ApiFormat::Gemini});
  // Small caps exercise discard mode and the complete-event skip; 1 is the
  // proto validation floor.
  const uint32_t max_event_size = provider.ConsumeIntegralInRange<uint32_t>(1, 4 * 1024);
  const uint64_t frag_seed = provider.ConsumeIntegral<uint64_t>();
  const std::string body = provider.ConsumeRemainingBytesAsString();

  Stats::TestUtil::TestStore store;
  AiProtocolManagerStats stats{
      ALL_AI_PROTOCOL_MANAGER_STATS(POOL_COUNTER_PREFIX(*store.rootScope(), "fuzz."))};

  // Fragmented run: pseudo-random frame sizes derived from the fuzz input.
  SseResponseHandler fragmented(format, max_event_size, /*max_parsed_events=*/1 << 20, stats);
  uint64_t state = frag_seed | 1;
  size_t offset = 0;
  while (offset < body.size()) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    const size_t frame_size = 1 + (state >> 33) % 97;
    Buffer::OwnedImpl frame(absl::string_view(body).substr(offset, frame_size));
    fragmented.onData(frame);
    offset += std::min(frame_size, body.size() - offset);
    FUZZ_ASSERT(SseResponseHandlerPeer::bufferedBytes(fragmented) <= max_event_size);
  }
  fragmented.onEndStream();

  // Control run: the same bytes in one frame.
  SseResponseHandler whole(format, max_event_size, /*max_parsed_events=*/1 << 20, stats);
  Buffer::OwnedImpl all(body);
  whole.onData(all);
  FUZZ_ASSERT(SseResponseHandlerPeer::bufferedBytes(whole) <= max_event_size);
  whole.onEndStream();

  // Extraction must be fragmentation-independent.
  const TokenUsage& a = fragmented.usage();
  const TokenUsage& b = whole.usage();
  FUZZ_ASSERT(a.input_tokens == b.input_tokens);
  FUZZ_ASSERT(a.output_tokens == b.output_tokens);
  FUZZ_ASSERT(a.total_tokens == b.total_tokens);
  FUZZ_ASSERT(a.cached_input_tokens == b.cached_input_tokens);
  FUZZ_ASSERT(a.cache_creation_input_tokens == b.cache_creation_input_tokens);
  FUZZ_ASSERT(a.tool_use_input_tokens == b.tool_use_input_tokens);
  FUZZ_ASSERT(a.reasoning_tokens == b.reasoning_tokens);
  FUZZ_ASSERT(a.model == b.model);
  FUZZ_ASSERT(a.api_format == b.api_format);
  FUZZ_ASSERT(fragmented.parsingComplete() == whole.parsingComplete());
  FUZZ_ASSERT(fragmented.degraded() == whole.degraded());

  // Canonicalization must be fragmentation-independent too.
  TokenUsage a_final = a;
  TokenUsage b_final = b;
  a_final.finalize();
  b_final.finalize();
  FUZZ_ASSERT(a_final.input_tokens == b_final.input_tokens);
  FUZZ_ASSERT(a_final.output_tokens == b_final.output_tokens);
  FUZZ_ASSERT(a_final.total_tokens == b_final.total_tokens);
  FUZZ_ASSERT(a_final.reported_total_tokens == b_final.reported_total_tokens);
  FUZZ_ASSERT(a_final.canonicalizationOverflow() == b_final.canonicalizationOverflow());

  // The JSON handler shares the accumulator; a quick no-crash pass.
  JsonResponseHandler json_handler(format, max_event_size, stats);
  Buffer::OwnedImpl json_body(body);
  json_handler.onData(json_body);
  json_handler.onEndStream();
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
