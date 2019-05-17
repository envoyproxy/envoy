#include "extensions/tracers/zipkin/span_buffer.h"

#include "zipkin.pb.h"

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

std::string SpanBuffer::serialize(SpanSerializer& serializer) {
  return serializer.serialize(span_buffer_);
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
