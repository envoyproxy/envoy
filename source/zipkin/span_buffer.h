#pragma once

#include <vector>
#include <memory>

#include "zipkin/zipkin_core_types.h"

namespace Zipkin {

typedef std::shared_ptr<Span> SpanPtr;

class SpanBuffer {
public:
  SpanBuffer() : size_(0), next_position_(0) {}

  SpanBuffer(uint64_t size) : span_buffer_(size, nullptr), size_(size), next_position_(0) {}

  virtual ~SpanBuffer() {}

  void allocateBuffer(uint64_t size);

  bool addSpan(Span&& span);

  void flush();

  uint64_t pendingSpans() { return next_position_; }

  std::string toStringifiedJsonArray();

private:
  // We use a pre-allocated vector to improve performance
  std::vector<SpanPtr> span_buffer_;
  uint64_t size_;
  uint64_t next_position_;
};
} // Zipkin
