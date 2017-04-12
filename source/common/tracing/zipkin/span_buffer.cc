#include "common/tracing/zipkin/span_buffer.h"

namespace Zipkin {

void SpanBuffer::allocateBuffer(uint64_t size) {
  span_buffer_.resize(size);
  size_ = size;
}

bool SpanBuffer::addSpan(Span&& span) {
  if (next_position_ == size_) {
    // Buffer full
    return false;
  }
  span_buffer_[next_position_++] = std::make_shared<Span>(span);

  return true;
}

void SpanBuffer::flush() {
  next_position_ = 0;
  std::fill(span_buffer_.begin(), span_buffer_.end(), nullptr);
}

std::string SpanBuffer::toStringifiedJsonArray() {
  std::string stringified_json_array = "[";

  if (pendingSpans()) {
    stringified_json_array += span_buffer_[0]->toJson();
    for (uint64_t i = 1; i < next_position_; i++) {
      stringified_json_array += ",";
      stringified_json_array += span_buffer_[i]->toJson();
    }
  }
  stringified_json_array += "]";

  return stringified_json_array;
}
} // Zipkin
