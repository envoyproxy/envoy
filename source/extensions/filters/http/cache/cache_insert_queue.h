#pragma once

#include <deque>
#include <functional>

#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class InsertQueueCallbacks {
public:
  virtual void insertQueueOverHighWatermark() PURE;
  virtual void insertQueueUnderLowWatermark() PURE;
  virtual void insertQueueAborted() PURE;
  virtual ~InsertQueueCallbacks() = default;
};
class CacheInsertFragment;

// This queue acts as an intermediary between CacheFilter and the cache
// implementation extension. Having a queue allows CacheFilter to stream at its
// normal rate, while allowing a cache implementation to run asynchronously and
// potentially at a slower rate, without having to implement its own buffer.
//
// If the queue contains more than the "high watermark" for the buffer
// (encoder_callbacks.encoderBufferLimit()), then a high watermark event is
// sent to the encoder, which may cause the filter to slow down, to allow the
// cache implementation time to catch up and avoid buffering significantly
// more data in memory than the configuration intends to allow. When this happens,
// the queue must drain to half the encoderBufferLimit before a low watermark
// event is sent to resume normal flow.
//
// From the cache implementation's perspective, the queue ensures that the cache
// receives data one piece at a time - no more data will be delivered until the
// cache implementation calls the provided callback indicating that it is ready
// to receive more data.
class CacheInsertQueue {
public:
  CacheInsertQueue(std::shared_ptr<HttpCache> cache,
                   Http::StreamEncoderFilterCallbacks& encoder_callbacks,
                   InsertContextPtr insert_context, InsertQueueCallbacks& callbacks);
  void insertHeaders(const Http::ResponseHeaderMap& response_headers,
                     const ResponseMetadata& metadata, bool end_stream);
  void insertBody(const Buffer::Instance& fragment, bool end_stream);
  void insertTrailers(const Http::ResponseTrailerMap& trailers);
  void setSelfOwned(std::unique_ptr<CacheInsertQueue> self);
  ~CacheInsertQueue();

private:
  void onFragmentComplete(bool cache_success, bool end_stream, size_t sz);

  Event::Dispatcher& dispatcher_;
  const InsertContextPtr insert_context_;
  const size_t low_watermark_bytes_, high_watermark_bytes_;
  OptRef<InsertQueueCallbacks> callbacks_;
  std::deque<std::unique_ptr<CacheInsertFragment>> fragments_;
  // Size of the data currently in the queue (including any fragment in flight).
  size_t queue_size_bytes_ = 0;
  // True when the high watermark has been exceeded and the low watermark
  // threshold has not been crossed since.
  bool watermarked_ = false;
  // True when the queue has sent a fragment to the cache implementation and has
  // not yet received a response.
  bool fragment_in_flight_ = false;
  // True if end_stream has been queued. If the queue gets handed ownership
  // of itself before the end is in sight then it might as well abort since
  // it's not going to get a complete entry.
  bool end_stream_queued_ = false;
  // If the filter was deleted while !end_stream_queued_, aborting_ is set to
  // true; when the next fragment completes (or cancels), the queue is destroyed.
  bool aborting_ = false;
  // When the filter is destroyed, it passes ownership of CacheInsertQueue
  // to itself, because CacheInsertQueue can outlive the filter. The queue
  // will remove its self-ownership (thereby deleting itself) upon
  // completion of its work.
  std::unique_ptr<CacheInsertQueue> self_ownership_;
  // The queue needs to keep a copy of the cache alive; if only the filter
  // keeps the cache alive then it's possible for the filter config to be deleted
  // while a cache action is still in flight, which can cause the cache to be
  // deleted prematurely.
  std::shared_ptr<HttpCache> cache_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
