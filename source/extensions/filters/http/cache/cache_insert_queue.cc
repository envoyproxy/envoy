#include "source/extensions/filters/http/cache/cache_insert_queue.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Representation of a piece of data to be sent to a cache for writing.
class CacheInsertFragment {
public:
  // Sends a fragment to the cache.
  // on_complete is called when the cache completes the operation.
  virtual void
  send(InsertContext& context,
       absl::AnyInvocable<void(bool cache_success, bool end_stream, size_t sz)> on_complete) PURE;

  virtual ~CacheInsertFragment() = default;
};

// A CacheInsertFragment containing some amount of http response body data.
// The size of a fragment is equal to the size of the buffer arriving at
// CacheFilter::encodeData.
class CacheInsertFragmentBody : public CacheInsertFragment {
public:
  CacheInsertFragmentBody(const Buffer::Instance& buffer, bool end_stream)
      : buffer_(buffer), end_stream_(end_stream) {}

  void send(InsertContext& context,
            absl::AnyInvocable<void(bool cache_success, bool end_stream, size_t sz)> on_complete)
      override {
    size_t sz = buffer_.length();
    context.insertBody(
        std::move(buffer_),
        [cb = std::move(on_complete), end_stream = end_stream_, sz](bool cache_success) mutable {
          std::move(cb)(cache_success, end_stream, sz);
        },
        end_stream_);
  }

private:
  Buffer::OwnedImpl buffer_;
  const bool end_stream_;
};

// A CacheInsertFragment containing the full trailers of the response.
class CacheInsertFragmentTrailers : public CacheInsertFragment {
public:
  explicit CacheInsertFragmentTrailers(const Http::ResponseTrailerMap& trailers)
      : trailers_(Http::ResponseTrailerMapImpl::create()) {
    Http::ResponseTrailerMapImpl::copyFrom(*trailers_, trailers);
  }

  void send(InsertContext& context,
            absl::AnyInvocable<void(bool cache_success, bool end_stream, size_t sz)> on_complete)
      override {
    // While zero isn't technically true for the size of trailers, it doesn't
    // matter at this point because watermarks after the stream is complete
    // aren't useful.
    context.insertTrailers(*trailers_, [cb = std::move(on_complete)](bool cache_success) mutable {
      std::move(cb)(cache_success, true, 0);
    });
  }

private:
  std::unique_ptr<Http::ResponseTrailerMap> trailers_;
};

CacheInsertQueue::CacheInsertQueue(std::shared_ptr<HttpCache> cache,
                                   Http::StreamEncoderFilterCallbacks& encoder_callbacks,
                                   InsertContextPtr insert_context, InsertQueueCallbacks& callbacks)
    : dispatcher_(encoder_callbacks.dispatcher()), insert_context_(std::move(insert_context)),
      low_watermark_bytes_(encoder_callbacks.encoderBufferLimit() / 2),
      high_watermark_bytes_(encoder_callbacks.encoderBufferLimit()), callbacks_(callbacks),
      cache_(cache) {}

void CacheInsertQueue::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                     const ResponseMetadata& metadata, bool end_stream) {
  end_stream_queued_ = end_stream;
  // While zero isn't technically true for the size of headers, headers are
  // typically excluded from the stream buffer limit.
  fragment_in_flight_ = true;
  insert_context_->insertHeaders(
      response_headers, metadata,
      [this, end_stream](bool cache_success) { onFragmentComplete(cache_success, end_stream, 0); },
      end_stream);
  // This requirement simplifies the cache implementation; most caches will have to
  // do asynchronous operations, and so will post anyway. It is an error to call continueDecoding
  // during decodeHeaders, and calling a callback inline *may* do that, therefore we
  // require the cache to post. A previous version performed a post here to guarantee
  // correct behavior, but that meant for async caches it would double-post - it makes
  // more sense to single-post when it may not be necessary (in the rarer case of a cache
  // not needing async action) than to double-post in the common async case.
  // This requirement may become unnecessary after some more iterations result in
  // continueDecoding no longer being a thing in this filter.
  ASSERT(fragment_in_flight_,
         "insertHeaders must post the callback to dispatcher, not just call it");
}

void CacheInsertQueue::insertBody(const Buffer::Instance& fragment, bool end_stream) {
  if (end_stream) {
    end_stream_queued_ = true;
  }
  if (fragment_in_flight_) {
    size_t sz = fragment.length();
    queue_size_bytes_ += sz;
    fragments_.push_back(std::make_unique<CacheInsertFragmentBody>(fragment, end_stream));
    if (!watermarked_ && queue_size_bytes_ > high_watermark_bytes_) {
      if (callbacks_.has_value()) {
        callbacks_->insertQueueOverHighWatermark();
      }
      watermarked_ = true;
    }
  } else {
    fragment_in_flight_ = true;
    insert_context_->insertBody(
        Buffer::OwnedImpl(fragment),
        [this, end_stream](bool cache_success) {
          onFragmentComplete(cache_success, end_stream, 0);
        },
        end_stream);
    ASSERT(fragment_in_flight_,
           "insertBody must post the callback to dispatcher, not just call it");
  }
}

void CacheInsertQueue::insertTrailers(const Http::ResponseTrailerMap& trailers) {
  end_stream_queued_ = true;
  if (fragment_in_flight_) {
    fragments_.push_back(std::make_unique<CacheInsertFragmentTrailers>(trailers));
  } else {
    fragment_in_flight_ = true;
    insert_context_->insertTrailers(
        trailers, [this](bool cache_success) { onFragmentComplete(cache_success, true, 0); });
    ASSERT(fragment_in_flight_,
           "insertTrailers must post the callback to dispatcher, not just call it");
  }
}

void CacheInsertQueue::onFragmentComplete(bool cache_success, bool end_stream, size_t sz) {
  ASSERT(dispatcher_.isThreadSafe());
  fragment_in_flight_ = false;
  if (aborting_) {
    // Parent filter was destroyed, so we can quit this operation.
    fragments_.clear();
    self_ownership_.reset();
    return;
  }
  ASSERT(queue_size_bytes_ >= sz, "queue can't be emptied by more than its size");
  queue_size_bytes_ -= sz;
  if (watermarked_ && queue_size_bytes_ <= low_watermark_bytes_) {
    if (callbacks_.has_value()) {
      callbacks_->insertQueueUnderLowWatermark();
    }
    watermarked_ = false;
  }
  if (!cache_success) {
    // canceled by cache; unwatermark if necessary, inform the filter if
    // it's still around, and delete the queue.
    if (watermarked_) {
      if (callbacks_.has_value()) {
        callbacks_->insertQueueUnderLowWatermark();
      }
      watermarked_ = false;
    }
    fragments_.clear();
    // Clearing self-ownership might provoke the destructor, so take a copy of the
    // abort callback to avoid reading from 'this' after it may be deleted.
    //
    // This complexity is necessary because if the queue *is not* currently
    // self-owned, it will be deleted during insertQueueAborted, so
    // clearing self_ownership_ second would be a write-after-destroy error.
    // If it *is* currently self-owned, then we must still call the callback if
    // any, but clearing self_ownership_ *first* would mean we got destroyed
    // so we would no longer have access to the callback.
    // Since destroying first *or* second can be an error, rearrange things
    // so that destroying first *is not* an error. :)
    auto callbacks = std::move(callbacks_);
    self_ownership_.reset();
    if (callbacks.has_value()) {
      callbacks->insertQueueAborted();
    }
    return;
  }
  if (end_stream) {
    ASSERT(fragments_.empty(), "ending a stream with the queue not empty is a bug");
    ASSERT(!watermarked_, "being over the high watermark when the queue is empty makes no sense");
    self_ownership_.reset();
    return;
  }
  if (!fragments_.empty()) {
    // If there's more in the queue, push the next fragment to the cache.
    auto fragment = std::move(fragments_.front());
    fragments_.pop_front();
    fragment_in_flight_ = true;
    fragment->send(*insert_context_, [this](bool cache_success, bool end_stream, size_t sz) {
      onFragmentComplete(cache_success, end_stream, sz);
    });
  }
}

void CacheInsertQueue::setSelfOwned(std::unique_ptr<CacheInsertQueue> self) {
  // If we sent a high watermark event, this is our last chance to unset it on the
  // stream, so we'd better do so.
  if (watermarked_) {
    if (callbacks_.has_value()) {
      callbacks_->insertQueueUnderLowWatermark();
    }
    watermarked_ = false;
  }
  // Disable all the callbacks, they're going to have nowhere to go.
  callbacks_.reset();
  if (fragments_.empty() && !fragment_in_flight_) {
    // If the queue is already empty we can just let it be destroyed immediately.
    return;
  }
  if (!end_stream_queued_) {
    // If the queue can't be completed we can abort early but we need to wait for
    // any callback-in-flight to complete before destroying the queue.
    aborting_ = true;
  }
  self_ownership_ = std::move(self);
}

CacheInsertQueue::~CacheInsertQueue() {
  ASSERT(!watermarked_, "should not have a watermarked status when the queue is destroyed");
  ASSERT(fragments_.empty(), "queue should be empty by the time the destructor is run");
  insert_context_->onDestroy();
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
