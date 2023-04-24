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
  bool end_stream_;
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

CacheInsertQueue::CacheInsertQueue(Event::Dispatcher& dispatcher, InsertContextPtr insert_context,
                                   size_t high_watermark_bytes, size_t low_watermark_bytes,
                                   OverHighWatermarkCallback high, UnderLowWatermarkCallback low,
                                   AbortInsertCallback abort)
    : dispatcher_(dispatcher), insert_context_(std::move(insert_context)),
      high_watermark_bytes_(high_watermark_bytes), low_watermark_bytes_(low_watermark_bytes),
      over_high_watermark_callback_(high), under_low_watermark_callback_(low),
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
  dispatcher_.post([this, ready, end_stream, sz]() {
    if (aborting_) {
      // Parent filter was destroyed, so we can quit this operation.
      self_ownership_.reset();
      return;
    }
    ASSERT(queue_size_bytes_ >= sz);
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
      abort_callback_();
      chunks_.clear();
      self_ownership_.reset();
      return;
    }
    if (end_stream) {
      // We should only complete with end_stream with an empty queue for obvious reasons.
      ASSERT(chunks_.empty());
      // If the queue is empty we should not be over the high watermark.
      ASSERT(!sent_watermark_);
      self_ownership_.reset();
      return;
    }
    if (!chunks_.empty()) {
      // If there's more in the queue, push the next chunk to the cache.
      auto chunk = std::move(chunks_.front());
      chunks_.pop_front();
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
    insert_context_->onDestroy();
    insert_context_ = nullptr;
  }
  self_ownership_ = std::move(self);
}

CacheInsertQueue::~CacheInsertQueue() {
  if (insert_context_) {
    insert_context_->onDestroy();
  }
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
