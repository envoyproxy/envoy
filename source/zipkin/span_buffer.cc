#include "span_buffer.h"

namespace Zipkin {

bool SpanBuffer::addSpan(Span&& span) {
  if (next_position_ == size_) {
    // Buffer full
    return false;
  }
  span_buffer_[next_position_++] = &span;

  return true;
}

void SpanBuffer::flush() {
  next_position_ = 0;
  std::fill(span_buffer_.begin(), span_buffer_.end(), nullptr);
}
} // Zipkin
