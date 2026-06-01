#pragma once

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

class SseResponseExtractor {
public:
  SseResponseExtractor() = default;

  // Processes an incoming chunk of an SSE stream and returns any completed event payloads.
  // Set end_stream to true if this is the final chunk of the response.
  std::vector<std::string> processChunk(absl::string_view chunk, bool end_stream = false);

private:
  std::string buffer_;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
