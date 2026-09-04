#include "source/extensions/filters/http/mcp_json_rest_bridge/sse_response_extractor.h"

#include <string>
#include <utility>
#include <vector>

#include "source/common/http/sse/sse_parser.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

absl::StatusOr<std::vector<std::string>> SseResponseExtractor::processChunk(absl::string_view chunk,
                                                                            bool end_stream) {
  if (max_response_body_size_ > 0 && (buffer_.size() + chunk.size()) > max_response_body_size_) {
    return absl::InvalidArgumentError("Response body limit exceeded");
  }
  std::vector<std::string> event_payloads;
  buffer_.append(chunk.data(), chunk.size());

  absl::string_view buffer_view = buffer_;
  const uint64_t length = buffer_.size();

  while (!buffer_view.empty()) {
    // Safely handles chunk boundaries and all line-ending formats
    Http::Sse::SseParser::FindEventEndResult result =
        Http::Sse::SseParser::findEventEnd(buffer_view, end_stream);

    // npos means the event hasn't reached a double blank line yet
    if (result.event_start == absl::string_view::npos) {
      break;
    }

    absl::string_view event_str =
        buffer_view.substr(result.event_start, result.event_end - result.event_start);

    Http::Sse::SseParser::ParsedEvent event = Http::Sse::SseParser::parseEvent(event_str);

    if (event.data.has_value()) {
      event_payloads.push_back(*std::move(event.data));
    }

    buffer_view = buffer_view.substr(result.next_start);
  }

  buffer_.erase(0, length - buffer_view.size());

  return event_payloads;
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
