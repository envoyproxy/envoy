#pragma once

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

class SseResponseExtractor {
public:
  explicit SseResponseExtractor(uint64_t max_response_body_size = 0)
      : max_response_body_size_(max_response_body_size) {}

  // Processes an incoming chunk of an SSE stream and returns any completed event payloads.
  // Set end_stream to true if this is the final chunk of the response.
  absl::StatusOr<std::vector<std::string>> processChunk(absl::string_view chunk, bool end_stream);

private:
  std::string buffer_;
  const uint64_t max_response_body_size_;
};

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
