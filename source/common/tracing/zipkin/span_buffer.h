#pragma once

#include "common/tracing/zipkin/zipkin_core_types.h"

namespace Zipkin {

typedef std::shared_ptr<Span> SpanSharedPtr;

/**
 * This class implements a simple buffer to store Zipkin tracing spans
 * prior to flushing them.
 */
class SpanBuffer {
public:
  /**
   * Constructor that creates an empty buffer. Space needs to be allocated by invoking
   * the method allocateBuffer(size).
   */
  SpanBuffer() : next_position_(0) {}

  /**
   * Constructor that initializes a buffer with the given size.
   *
   * @param size The desired buffer size.
   */
  SpanBuffer(uint64_t size) : span_buffer_(size, nullptr), next_position_(0) {}

  /**
   * Destructor.
   */
  virtual ~SpanBuffer() {}

  /**
   * Allocates space for an empty buffer or resizes a previously-allocated one.
   *
   * @param size The desired buffer size.
   */
  void allocateBuffer(uint64_t size);

  /**
   * Adds the given Zipkin span to the buffer.
   *
   * @param span The span to be added to the buffer.
   *
   * @return true if the span was successfully added, or false if the buffer was full.
   */
  bool addSpan(Span&& span);

  /**
   * Empties the buffer. This method is supposed to be called when all buffered spans
   * have been sent to to the Zipkin service.
   */
  void clear();

  /**
   * @return the number of spans currently buffered.
   */
  uint64_t pendingSpans() { return next_position_; }

  /**
   * @return the contents of the buffer as a stringified array of JSONs, where
   * each JSON in the array corresponds to one Zipkin span.
   */
  std::string toStringifiedJsonArray();

private:
  // We use a pre-allocated vector to improve performance
  std::vector<SpanSharedPtr> span_buffer_;
  uint64_t next_position_;
};
} // Zipkin
