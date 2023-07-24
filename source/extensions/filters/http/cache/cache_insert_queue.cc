#include "source/extensions/filters/http/cache/cache_insert_queue.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheInsertChunk {
public:
  // Sends a chunk to the cache.
  // on_complete is called when the cache completes the operation.
  virtual void send(InsertContext& context,
                    std::function<void(bool ready, bool end_stream, size_t sz)> on_complete) PURE;

  virtual ~CacheInsertChunk() = default;
};

class CacheInsertChunkBody : public CacheInsertChunk {
public:
  CacheInsertChunkBody(const Buffer::Instance& buffer, bool end_stream)
      : buffer_(buffer), end_stream_(end_stream) {}

  void send(InsertContext& context,
            std::function<void(bool ready, bool end_stream, size_t sz)> on_complete) override {
    size_t sz = buffer_.length();
    context.insertBody(
        std::move(buffer_),
        [on_complete, end_stream = end_stream_, sz](bool ready) {
          on_complete(ready, end_stream, sz);
        },
        end_stream_);
  }

private:
  Buffer::OwnedImpl buffer_;
  const bool end_stream_;
};

class CacheInsertChunkTrailers : public CacheInsertChunk {
public:
  explicit CacheInsertChunkTrailers(const Http::ResponseTrailerMap& trailers)
      : trailers_(Http::ResponseTrailerMapImpl::create()) {
    Http::ResponseTrailerMapImpl::copyFrom(*trailers_, trailers);
  }

  void send(InsertContext& context,
            std::function<void(bool ready, bool end_stream, size_t sz)> on_complete) override {
    // While zero isn't technically true for the size of trailers, it doesn't
    // matter at this point because watermarks after the stream is complete
    // aren't useful.
    context.insertTrailers(*trailers_, [on_complete](bool ready) { on_complete(ready, true, 0); });
  }

private:
  std::unique_ptr<Http::ResponseTrailerMap> trailers_;
};

// We capture the values from encoder_callbacks, and the callbacks as lambdas, so
// that we don't have to keep checking whether the filter has been deleted.
// When the filter *is* deleted, we simply replace all the callback lambdas with no-op.
CacheInsertQueue::CacheInsertQueue(Http::StreamEncoderFilterCallbacks& encoder_callbacks,
                                   InsertContextPtr insert_context, AbortInsertCallback abort)
    : dispatcher_(encoder_callbacks.dispatcher()), insert_context_(std::move(insert_context)),
      low_watermark_bytes_(encoder_callbacks.encoderBufferLimit() / 2),
      high_watermark_bytes_(encoder_callbacks.encoderBufferLimit()),
      under_low_watermark_callback_([&encoder_callbacks]() {
        encoder_callbacks.onEncoderFilterBelowWriteBufferLowWatermark();
      }),
      over_high_watermark_callback_([&encoder_callbacks]() {
        encoder_callbacks.onEncoderFilterAboveWriteBufferHighWatermark();
      }),
      abort_callback_(abort) {}

void CacheInsertQueue::insertHeaders(const Http::ResponseHeaderMap& response_headers,
                                     const ResponseMetadata& metadata, bool end_stream) {
  if (end_stream) {
    end_in_sight_ = true;
  }
  // While zero isn't technically true for the size of headers, headers are
  // typically excluded from the stream buffer limit.
  insert_context_->insertHeaders(
      response_headers, metadata,
      [this, end_stream](bool ready) { onChunkComplete(ready, end_stream, 0); }, end_stream);
  chunk_in_flight_ = true;
}

void CacheInsertQueue::insertBody(const Buffer::Instance& chunk, bool end_stream) {
  if (end_stream) {
    end_in_sight_ = true;
  }
  if (chunk_in_flight_) {
    size_t sz = chunk.length();
    queue_size_bytes_ += sz;
    chunks_.push_back(std::make_unique<CacheInsertChunkBody>(chunk, end_stream));
    if (!sent_watermark_ && queue_size_bytes_ > high_watermark_bytes_) {
      over_high_watermark_callback_();
      sent_watermark_ = true;
    }
  } else {
    insert_context_->insertBody(
        Buffer::OwnedImpl(chunk),
        [this, end_stream](bool ready) { onChunkComplete(ready, end_stream, 0); }, end_stream);
    chunk_in_flight_ = true;
  }
}

void CacheInsertQueue::insertTrailers(const Http::ResponseTrailerMap& trailers) {
  end_in_sight_ = true;
  if (chunk_in_flight_) {
    chunks_.push_back(std::make_unique<CacheInsertChunkTrailers>(trailers));
  } else {
    insert_context_->insertTrailers(trailers,
                                    [this](bool ready) { onChunkComplete(ready, true, 0); });
    chunk_in_flight_ = true;
  }
}

void CacheInsertQueue::onChunkComplete(bool ready, bool end_stream, size_t sz) {
  // If the cache implementation is asynchronous, this may be called from whatever
  // thread that cache implementation runs on. Therefore, we post it to the
  // dispatcher to be certain any callbacks and updates are called on the filter's
  // thread (and therefore we don't have to mutex-guard anything).
  dispatcher_.post([this, ready, end_stream, sz]() {
    chunk_in_flight_ = false;
    if (aborting_) {
      // Parent filter was destroyed, so we can quit this operation.
      self_ownership_.reset();
      return;
    }
    ASSERT(queue_size_bytes_ >= sz, "queue can't be emptied by more than its size");
    queue_size_bytes_ -= sz;
    if (sent_watermark_ && queue_size_bytes_ <= low_watermark_bytes_) {
      under_low_watermark_callback_();
      sent_watermark_ = false;
    }
    if (!ready) {
      // canceled by cache; unwatermark if necessary, inform the filter if
      // it's still around, and delete the queue.
      if (sent_watermark_) {
        under_low_watermark_callback_();
      }
      chunks_.clear();
      self_ownership_.reset();
      abort_callback_();
      return;
    }
    if (end_stream) {
      ASSERT(chunks_.empty(), "ending a stream with the queue not empty is a bug");
      ASSERT(!sent_watermark_,
             "being over the high watermark when the queue is empty makes no sense");
      self_ownership_.reset();
      return;
    }
    if (!chunks_.empty()) {
      // If there's more in the queue, push the next chunk to the cache.
      auto chunk = std::move(chunks_.front());
      chunks_.pop_front();
      chunk_in_flight_ = true;
      chunk->send(*insert_context_, [this](bool ready, bool end_stream, size_t sz) {
        onChunkComplete(ready, end_stream, sz);
      });
    }
  });
}

void CacheInsertQueue::takeOwnershipOfYourself(std::unique_ptr<CacheInsertQueue> self) {
  // Disable all the callbacks, they're going to have nowhere to go.
  abort_callback_ = under_low_watermark_callback_ = over_high_watermark_callback_ = []() {};
  if (chunks_.empty() && !chunk_in_flight_) {
    // If the queue is already empty we can just let it be destroyed immediately.
    return;
  }
  if (!end_in_sight_) {
    // If the queue can't be completed we can abort early but we need to wait for
    // any callback-in-flight to complete before destroying the queue.
    aborting_ = true;
  }
  self_ownership_ = std::move(self);
}

CacheInsertQueue::~CacheInsertQueue() { insert_context_->onDestroy(); }

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
