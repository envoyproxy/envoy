#include "extensions/tracers/zipkin/span_buffer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

// TODO(fabolive): Need to avoid the copy to improve performance.
bool SpanBuffer::addSpan(const Span& span) {
  if (span_buffer_.size() == span_buffer_.capacity()) {
    // Buffer full
    return false;
  }
  span_buffer_.push_back(std::move(span));

  return true;
}

std::string SpanBuffer::toStringifiedJsonArray() {
  std::string stringified_json_array = "[";

  if (pendingSpans()) {
    stringified_json_array += span_buffer_[0].toJson();
    const uint64_t size = span_buffer_.size();
    for (uint64_t i = 1; i < size; i++) {
      stringified_json_array += ",";
      stringified_json_array += span_buffer_[i].toJson();
    }
  }
  stringified_json_array += "]";

  return stringified_json_array;
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
