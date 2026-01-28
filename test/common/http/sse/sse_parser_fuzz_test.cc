#include "source/common/http/sse/sse_parser.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

// Fuzz test for SSE parser functions.
// This tests the parser with arbitrary input to catch crashes, hangs, and memory issues.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const absl::string_view input(reinterpret_cast<const char*>(buf), len);

  // Fuzz parseEvent with arbitrary input
  Http::Sse::SseParser::parseEvent(input);

  // Fuzz findEventEnd with end_stream = false
  Http::Sse::SseParser::findEventEnd(input, false);

  // Fuzz findEventEnd with end_stream = true
  Http::Sse::SseParser::findEventEnd(input, true);

  auto result = Http::Sse::SseParser::findEventEnd(input, false);
  if (result.event_start != absl::string_view::npos) {
    absl::string_view event =
        input.substr(result.event_start, result.event_end - result.event_start);
    Http::Sse::SseParser::parseEvent(event);

    // If there's more data after the event, continue parsing
    if (result.next_start < input.size()) {
      absl::string_view remaining = input.substr(result.next_start);
      Http::Sse::SseParser::findEventEnd(remaining, false);
      Http::Sse::SseParser::findEventEnd(remaining, true);
    }
  }

  // Fuzz with BOM prefixed to input (randomly, based on first byte)
  if (len > 0 && buf[0] % 4 == 0) { // 25% of inputs get BOM prefix
    std::string bom_input = std::string("\xEF\xBB\xBF") + std::string(input);
    Http::Sse::SseParser::findEventEnd(bom_input, false);
    Http::Sse::SseParser::findEventEnd(bom_input, true);
    auto bom_result = Http::Sse::SseParser::findEventEnd(bom_input, false);
    if (bom_result.event_start != absl::string_view::npos) {
      absl::string_view bom_event = absl::string_view(bom_input).substr(
          bom_result.event_start, bom_result.event_end - bom_result.event_start);
      Http::Sse::SseParser::parseEvent(bom_event);
    }
  }

  // Fuzz with chunked input simulation at multiple split points
  // This simulates real-world chunked HTTP responses
  if (len > 1) {
    // Test at 5 evenly-spaced split points for better coverage
    for (size_t i = 1; i <= 5 && i < len; ++i) {
      size_t split = (len * i) / 6;
      const absl::string_view first_chunk = input.substr(0, split);
      const absl::string_view second_chunk = input.substr(split);

      // Try to find event in first chunk (may be incomplete)
      Http::Sse::SseParser::findEventEnd(first_chunk, false);
      Http::Sse::SseParser::findEventEnd(first_chunk, true);

      // Parse events from each chunk
      Http::Sse::SseParser::parseEvent(first_chunk);
      Http::Sse::SseParser::parseEvent(second_chunk);

      // Test concatenation: typical chunked streaming pattern
      if (split > 0 && split < len) {
        auto chunk_result = Http::Sse::SseParser::findEventEnd(first_chunk, false);
        // If no complete event in first chunk, data carries over to second chunk
        if (chunk_result.event_end == absl::string_view::npos) {
          std::string combined = std::string(first_chunk) + std::string(second_chunk);
          Http::Sse::SseParser::findEventEnd(combined, false);
          Http::Sse::SseParser::parseEvent(combined);
        }
      }
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
