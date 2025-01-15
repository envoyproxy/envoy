#include "source/extensions/filters/http/cache/active_cache_impl.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cacheability_utils.h"
#include "source/extensions/filters/http/cache/range_utils.h"
#include "source/extensions/filters/http/cache/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using CancelWrapper::cancelWrapped;

class UpstreamRequestWithHeadersPrepopulated : public HttpSource {
public:
  UpstreamRequestWithHeadersPrepopulated(std::unique_ptr<HttpSource> original_source,
                                         Http::ResponseHeaderMapPtr headers, EndStream end_stream)
      : original_source_(std::move(original_source)), headers_(std::move(headers)),
        end_stream_after_headers_(end_stream) {}
  void getHeaders(GetHeadersCallback&& cb) override {
    cb(std::move(headers_), end_stream_after_headers_);
  }
  // Calls the provided callback with a buffer that is the beginning of the
  // requested range, up to but not necessarily including the entire requested
  // range, or no buffer if there is no more data or an error occurred.
  void getBody(AdjustedByteRange range, GetBodyCallback&& cb) override {
    original_source_->getBody(std::move(range), std::move(cb));
  }
  virtual void getTrailers(GetTrailersCallback&& cb) override {
    original_source_->getTrailers(std::move(cb));
  }

private:
  std::unique_ptr<HttpSource> original_source_;
  Http::ResponseHeaderMapPtr headers_;
  EndStream end_stream_after_headers_;
};

static Http::RequestHeaderMapPtr
requestHeadersWithRangeRemoved(const Http::RequestHeaderMap& original_headers) {
  Http::RequestHeaderMapPtr headers =
      Http::createHeaderMap<Http::RequestHeaderMapImpl>(original_headers);
  headers->remove(Envoy::Http::Headers::get().Range);
  return headers;
}

static ActiveLookupResultPtr
upstreamPassThrough(ActiveLookupRequestPtr lookup,
                    CacheEntryStatus status = CacheEntryStatus::Uncacheable) {
  auto result = std::make_unique<ActiveLookupResult>();
  auto upstream = lookup->upstreamRequestFactory().create(lookup->requestHeaders());
  result->http_source_ = std::move(upstream);
  result->status_ = status;
  return result;
}

static Http::ResponseHeaderMapPtr notSatisfiableHeaders() {
  static const std::string not_satisfiable =
      std::to_string(enumToInt(Http::Code::RangeNotSatisfiable));
  return Http::createHeaderMap<Http::ResponseHeaderMapImpl>({
      {Http::Headers::get().Status, not_satisfiable},
      {Http::Headers::get().ContentLength, "0"},
  });
}

void ActiveLookupContext::getHeaders(GetHeadersCallback&& cb) {
  absl::optional<std::vector<RawByteRange>> ranges = lookup().parseRange();
  if (ranges) {
    // If it's a range request, inject the appropriate modified content-range and
    // content-length headers into the response once we have the response headers.
    entry_->wantHeaders(
        dispatcher(), lookup().timestamp(),
        [ranges = std::move(ranges.value()), cl = content_length_,
         cb = std::move(cb)](Http::ResponseHeaderMapPtr headers, EndStream end_stream) mutable {
          if (!headers) {
            return cb(nullptr, end_stream);
          }
          if (cl == 0 && headers->ContentLength()) {
            absl::SimpleAtoi(headers->getContentLengthValue(), &cl) || (cl = 0);
          }
          RangeDetails range_details = RangeUtils::createAdjustedRangeDetails(ranges, cl);
          if (!range_details.satisfiable_) {
            return cb(notSatisfiableHeaders(), EndStream::End);
          }
          if (range_details.ranges_.empty()) {
            return cb(std::move(headers), end_stream);
          }
          auto& range = range_details.ranges_[0];
          headers->setReferenceKey(
              Envoy::Http::Headers::get().ContentRange,
              fmt::format("bytes {}-{}/{}", range.begin(), range.end() - 1, cl));
          headers->setContentLength(range.length());
          static const std::string partial_content =
              std::to_string(enumToInt(Http::Code::PartialContent));
          headers->setStatus(partial_content);
          cb(std::move(headers), end_stream);
        });
  } else {
    entry_->wantHeaders(dispatcher(), lookup().timestamp(), std::move(cb));
  }
}

void ActiveLookupContext::getBody(AdjustedByteRange range, GetBodyCallback&& cb) {
  entry_->wantBodyRange(range, dispatcher(), std::move(cb));
}

void ActiveLookupContext::getTrailers(GetTrailersCallback&& cb) {
  entry_->wantTrailers(dispatcher(), std::move(cb));
}

std::shared_ptr<ActiveCache> ActiveCache::create(TimeSource& time_source,
                                                 std::unique_ptr<HttpCache> cache) {
  return std::make_shared<ActiveCacheImpl>(time_source, std::move(cache));
}

ActiveCacheEntry::ActiveCacheEntry(std::weak_ptr<ActiveCacheImpl> cache, const Key& key)
    : cache_(std::move(cache)), key_(key) {}

void ActiveCacheEntry::wantHeaders(Event::Dispatcher&, SystemTime lookup_timestamp,
                                   GetHeadersCallback&& cb) {
  Http::ResponseHeaderMapPtr headers;
  EndStream end_stream_after_headers;
  {
    absl::MutexLock lock(&mu_);
    ASSERT(entry_.response_headers_ != nullptr,
           "headers should have been initialized during lookup");
    headers = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*entry_.response_headers_);
    Seconds age = CacheHeadersUtils::calculateAge(
        *headers, entry_.response_metadata_.response_time_, lookup_timestamp);
    headers->setReferenceKey(Envoy::Http::CustomHeaders::get().Age, std::to_string(age.count()));
    end_stream_after_headers = endStreamAfterHeaders();
  }
  cb(std::move(headers), end_stream_after_headers);
}

void ActiveCacheEntry::wantBodyRange(AdjustedByteRange range, Event::Dispatcher& dispatcher,
                                     GetBodyCallback&& cb) {
  absl::MutexLock lock(&mu_);
  ASSERT(entry_.response_headers_ != nullptr,
         "body should not be requested when headers haven't been sent");
  body_subscribers_.emplace_back(dispatcher, std::move(range), std::move(cb));
  // if there's not already a body read operation in flight, start one.
  maybeTriggerBodyReadForWaitingSubscriber();
}

void ActiveCacheEntry::wantTrailers(Event::Dispatcher& dispatcher, GetTrailersCallback&& cb) {
  absl::MutexLock lock(&mu_);
  if (entry_.response_trailers_ != nullptr) {
    auto trailers = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*entry_.response_trailers_);
    dispatcher.post([cb = std::move(cb), trailers = std::move(trailers)]() mutable {
      cb(std::move(trailers), EndStream::End);
    });
    return;
  }
  ASSERT(!entry_.body_length_.has_value(),
         "wantTrailers should not be called when there are no trailers");
  trailer_subscribers_.emplace_back(dispatcher, std::move(cb));
}

void ActiveCacheEntry::onHeadersInserted(CacheReaderPtr cache_reader,
                                         Http::ResponseHeaderMapPtr headers, bool end_stream) {
  absl::MutexLock lock(&mu_);
  std::shared_ptr<ActiveCacheImpl> active_cache = cache_.lock();
  if (!active_cache) {
    ENVOY_LOG(error, "cache config was deleted while header-insertion was in flight");
    return onCacheWentAway();
  }
  entry_.cache_reader_ = std::move(cache_reader);
  entry_.response_headers_ = std::move(headers);
  entry_.response_metadata_ = active_cache->makeMetadata();
  if (end_stream) {
    insertComplete();
  } else {
    state_ = State::Inserting;
  }
  handleValidationAndSendLookupResponses(CacheEntryStatus::Miss);
}

bool ActiveCacheEntry::requiresValidationFor(const ActiveLookupRequest& lookup) const {
  mu_.AssertHeld();
  const Seconds age = CacheHeadersUtils::calculateAge(
      *entry_.response_headers_, entry_.response_metadata_.response_time_, lookup.timestamp());
  return lookup.requiresValidation(*entry_.response_headers_, age);
}

void ActiveCacheEntry::handleValidationAndSendLookupResponses(CacheEntryStatus status) {
  mu_.AssertHeld();
  ASSERT(state_ == State::Exists || state_ == State::Inserting);
  // Reorder subscribers so those who do not require validation are at the end,
  // and 'it' is the first subscriber that does not require validation.
  auto it = std::partition(lookup_subscribers_.begin(), lookup_subscribers_.end(),
                           [this](LookupSubscriber& s) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
                             return requiresValidationFor(s.context_->lookup());
                           });
  for (auto recipient = it; recipient != lookup_subscribers_.end(); recipient++) {
    sendSuccessfulLookupResultTo(*recipient, status);
    // If there was more than one recipient, and the first one was a miss, the
    // rest will be streamed.
    if (status == CacheEntryStatus::Miss) {
      status = CacheEntryStatus::Streamed;
    }
  }
  lookup_subscribers_.erase(it, lookup_subscribers_.end());
  if (!lookup_subscribers_.empty()) {
    // At least one subscriber required validation.
    performValidation();
  }
}

EndStream ActiveCacheEntry::endStreamAfterHeaders() const {
  mu_.AssertHeld();
  bool end_stream = entry_.body_length_.value_or(1) == 0 && entry_.response_trailers_ == nullptr;
  return end_stream ? EndStream::End : EndStream::More;
}

EndStream ActiveCacheEntry::endStreamAfterBody() const {
  mu_.AssertHeld();
  ASSERT(entry_.body_length_.has_value(),
         "should not be testing endStreamAfterBody if body not complete");
  return (entry_.response_trailers_ == nullptr) ? EndStream::End : EndStream::More;
}

void ActiveCacheEntry::sendSuccessfulLookupResultTo(LookupSubscriber& subscriber,
                                                    CacheEntryStatus status) {
  mu_.AssertHeld();
  ASSERT(state_ == State::Exists || state_ == State::Inserting);
  auto result = std::make_unique<ActiveLookupResult>();
  result->status_ = status;
  result->http_source_ = std::move(subscriber.context_);
  subscriber.dispatcher().post(
      [result = std::move(result), callback = std::move(subscriber.callback_)]() mutable {
        callback(std::move(result));
      });
}

void ActiveCacheEntry::onBodyInserted(AdjustedByteRange range, bool end_stream) {
  absl::MutexLock lock(&mu_);
  body_length_available_ = range.end();
  if (end_stream) {
    insertComplete();
  }
  maybeTriggerBodyReadForWaitingSubscriber();
}

void ActiveCacheEntry::onTrailersInserted(Http::ResponseTrailerMapPtr trailers) {
  ASSERT(trailers);
  absl::MutexLock lock(&mu_);
  entry_.response_trailers_ = std::move(trailers);
  insertComplete();
  for (TrailerSubscriber& subscriber : trailer_subscribers_) {
    sendTrailersTo(subscriber);
  }
  trailer_subscribers_.clear();
}

void ActiveCacheEntry::sendTrailersTo(TrailerSubscriber& subscriber) {
  mu_.AssertHeld();
  ASSERT(entry_.response_trailers_ != nullptr);
  subscriber.dispatcher().post(
      [trailers = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*entry_.response_trailers_),
       callback = std::move(subscriber.callback_)]() mutable {
        callback(std::move(trailers), EndStream::End);
      });
}

void ActiveCacheEntry::onInsertFailed() {
  absl::MutexLock lock(&mu_);
  ENVOY_LOG(error, "cache insert failed");
  onCacheError();
}

void ActiveCacheEntry::onCacheError() {
  mu_.AssertHeld();
  for (LookupSubscriber& sub : lookup_subscribers_) {
    sub.callback_(upstreamPassThrough(sub.context_->movedLookup(), CacheEntryStatus::LookupError));
  }
  for (BodySubscriber& sub : body_subscribers_) {
    sub.callback_(nullptr, EndStream::Reset);
  }
  for (TrailerSubscriber& sub : trailer_subscribers_) {
    sub.callback_(nullptr, EndStream::Reset);
  }
  lookup_subscribers_.clear();
  body_subscribers_.clear();
  trailer_subscribers_.clear();
  auto active_cache = cache_.lock();
  if (active_cache) {
    active_cache->cache().evict(*dispatcher_, key_);
  }
  state_ = State::New;
}

void ActiveCacheEntry::insertComplete() {
  mu_.AssertHeld();
  state_ = State::Exists;
  entry_.body_length_ = body_length_available_;
  if (content_length_header_ == entry_.body_length_) {
    return;
  }
  if (content_length_header_ != 0) {
    ENVOY_LOG(error,
              "cache insert for {}/{} had content-length header {} but actual size {}. Cache has "
              "modified the header to match actual size.",
              key_.host(), key_.path(), content_length_header_, entry_.body_length_.value());
  }
  content_length_header_ = body_length_available_;
}

void ActiveCacheEntry::abortBodyOutOfRangeSubscribers() {
  mu_.AssertHeld();
  if (!entry_.body_length_.has_value()) {
    // Don't know if a request is out of range until the available range is known.
    return;
  }
  // Any subscribers who requested an invalid range should be aborted now that
  // we know their range is invalid. Subscribers who asked for body starting at
  // the end of the range should receive null body.
  EndStream end_stream = endStreamAfterBody();
  body_subscribers_.erase(
      std::remove_if(body_subscribers_.begin(), body_subscribers_.end(),
                     [this, end_stream](BodySubscriber& bs) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
                       if (bs.range_.begin() >= body_length_available_) {
                         if (bs.range_.begin() == body_length_available_) {
                           auto cb = std::move(bs.callback_);
                           bs.dispatcher().post([cb = std::move(cb), end_stream]() mutable {
                             cb(nullptr, end_stream);
                           });
                         } else {
                           bs.callback_(nullptr, EndStream::Reset);
                         }
                         return true;
                       }
                       return false;
                     }),
      body_subscribers_.end());
}

void ActiveCacheEntry::maybeTriggerBodyReadForWaitingSubscriber() {
  mu_.AssertHeld();
  ASSERT(entry_.cache_reader_);
  if (cancel_action_in_flight_ != nullptr) {
    // There is already an action in flight so don't read more body yet.
    return;
  }
  abortBodyOutOfRangeSubscribers();
  auto it = std::find_if(
      body_subscribers_.begin(), body_subscribers_.end(),
      [this](BodySubscriber& subscriber) { return canReadBodyRangeFromCacheEntry(subscriber); });
  if (it == body_subscribers_.end()) {
    // There is nobody waiting to read some body that's available.
    return;
  }
  AdjustedByteRange range = it->range_;
  if (range.end() > body_length_available_) {
    range = AdjustedByteRange(range.begin(), body_length_available_);
  }
  if (range.length() > max_read_chunk_size_) {
    range = AdjustedByteRange(range.begin(), range.begin() + max_read_chunk_size_);
  }
  // Don't need this to be cancellable because there's a shared_ptr in the lambda keeping the
  // ActiveCacheEntry alive. We post to a thread before making the request for two reasons - we want
  // the request to be performed on the requester's worker thread for balance, and we want to be
  // able to lock the mutex again on the callback - if the cache called back immediately rather than
  // posting and we *didn't* post before making the request, the mutex would still be held
  // from this outer function so the callback would deadlock. By posting to a queue we ensure
  // that deadlock cannot occur.
  // Also, by ensuring the action occurs from a dispatcher queue, we guarantee that
  // the "trigger again" at the end of onBodyChunkFromCache can't build up to a stack overflow
  // of maybeTrigger->getBody->onBodyChunk->maybeTrigger->...
  it->dispatcher().post([&dispatcher = it->dispatcher(), p = shared_from_this(), range,
                         cache_reader = entry_.cache_reader_.get()]() mutable {
    cache_reader->getBody(
        dispatcher, range,
        [p = std::move(p), range](Buffer::InstancePtr buffer, EndStream end_stream) {
          p->onBodyChunkFromCache(std::move(range), std::move(buffer), end_stream);
        });
  });
}

bool ActiveCacheEntry::canReadBodyRangeFromCacheEntry(BodySubscriber& subscriber) {
  mu_.AssertHeld();
  return subscriber.range_.begin() < body_length_available_;
}

void ActiveCacheEntry::onBodyChunkFromCache(AdjustedByteRange range, Buffer::InstancePtr buffer,
                                            EndStream end_stream) {
  absl::MutexLock lock(&mu_);
  cancel_action_in_flight_ = nullptr;
  if (end_stream == EndStream::Reset) {
    ENVOY_LOG(error, "cache entry provoked reset");
    onCacheError();
    return;
  }
  if (buffer == nullptr) {
    IS_ENVOY_BUG("cache returned null buffer non-reset");
    return;
  }
  ASSERT(buffer->length() <= range.length());
  if (buffer->length() < range.length()) {
    range = AdjustedByteRange(range.begin(), range.begin() + buffer->length());
  }
  uint8_t* bytes = static_cast<uint8_t*>(buffer->linearize(range.length()));
  body_subscribers_.erase(
      std::remove_if(body_subscribers_.begin(), body_subscribers_.end(),
                     [this, &range, bytes](BodySubscriber& subscriber)
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
                           if (subscriber.range_.begin() >= range.begin() &&
                               subscriber.range_.begin() < range.end()) {
                             AdjustedByteRange r(subscriber.range_.begin(),
                                                 std::min(subscriber.range_.end(), range.end()));
                             sendBodyChunkTo(subscriber, r,
                                             std::make_unique<Buffer::OwnedImpl>(
                                                 bytes + r.begin() - range.begin(), r.length()));
                             return true;
                           }
                           return false;
                         }),
      body_subscribers_.end());
  maybeTriggerBodyReadForWaitingSubscriber();
}

void ActiveCacheEntry::sendBodyChunkTo(BodySubscriber& subscriber, AdjustedByteRange range,
                                       Buffer::InstancePtr buffer) {
  mu_.AssertHeld();
  bool end_stream = entry_.body_length_.has_value() && range.end() == entry_.body_length_.value() &&
                    entry_.response_trailers_ == nullptr;
  subscriber.dispatcher().post([end_stream, callback = std::move(subscriber.callback_),
                                buffer = std::move(buffer)]() mutable {
    callback(std::move(buffer), end_stream ? EndStream::End : EndStream::More);
  });
}

ActiveCacheEntry::~ActiveCacheEntry() {}

void ActiveCacheEntry::getLookupResult(ActiveCacheImpl& cache, ActiveLookupRequestPtr lookup,
                                       ActiveLookupResultCallback&& cb) {
  absl::MutexLock lock(&mu_);
  switch (state_) {
  case State::Vary:
    ASSERT(&cache); // Do another lookup
    IS_ENVOY_BUG("not implemented yet");
  case State::NotCacheable: {
    Event::Dispatcher& dispatcher = lookup->dispatcher();
    dispatcher.post([cb = std::move(cb), lookup = std::move(lookup)]() mutable {
      cb(upstreamPassThrough(std::move(lookup)));
    });
    return;
  }
  case State::Validating:
  case State::Pending:
    lookup_subscribers_.emplace_back(std::make_unique<ActiveLookupContext>(std::move(lookup),
                                                                           shared_from_this(),
                                                                           content_length_header_),
                                     std::move(cb));
    return;
  case State::Exists:
  case State::Inserting: {
    CacheEntryStatus status = CacheEntryStatus::Hit;
    if (requiresValidationFor(*lookup)) {
      if (state_ == State::Inserting) {
        // Skip validation if the cache write is still in progress.
        status = CacheEntryStatus::ValidatedFree;
      } else {
        lookup_subscribers_.emplace_back(
            std::make_unique<ActiveLookupContext>(std::move(lookup), shared_from_this(),
                                                  content_length_header_),
            std::move(cb));
        return performValidation();
      }
    }
    auto result = std::make_unique<ActiveLookupResult>();
    Event::Dispatcher& dispatcher = lookup->dispatcher();
    result->http_source_ = std::make_unique<ActiveLookupContext>(
        std::move(lookup), shared_from_this(), content_length_header_);
    result->status_ = status;
    dispatcher.post(
        [cb = std::move(cb), result = std::move(result)]() mutable { cb(std::move(result)); });
    return;
  }
  case State::New: {
    Event::Dispatcher& dispatcher = lookup->dispatcher();
    if (lookup->requestHeaders().getMethodValue() == Http::Headers::get().MethodValues.Head) {
      // HEAD requests are not cacheable, just pass through.
      dispatcher.post([cb = std::move(cb), lookup = std::move(lookup)]() mutable {
        cb(upstreamPassThrough(std::move(lookup), CacheEntryStatus::Uncacheable));
      });
      return;
    }
    LookupRequest request(Key{lookup->key()}, dispatcher);
    lookup_subscribers_.emplace_back(
        std::make_unique<ActiveLookupContext>(std::move(lookup), shared_from_this()),
        std::move(cb));
    state_ = State::Pending;
    std::shared_ptr<ActiveCacheImpl> active_cache = cache_.lock();
    ASSERT(active_cache, "should be impossible for cache entry to be deleted in getLookupResult");
    // posted to prevent callback mutex-deadlock.
    return dispatcher.post([active_cache = std::move(active_cache), p = shared_from_this(),
                            request = std::move(request)]() mutable {
      // p is captured as shared_ptr to ensure 'this' is not deleted while the
      // lookup is in flight.
      active_cache->cache().lookup(std::move(request),
                                   [p](absl::StatusOr<LookupResult>&& lookup_result) {
                                     p->onCacheLookupResult(std::move(lookup_result));
                                   });
    });
  }
  }
}

void ActiveCacheEntry::onCacheLookupResult(absl::StatusOr<LookupResult>&& lookup_result) {
  absl::MutexLock lock(&mu_);
  if (!lookup_result.ok()) {
    return onCacheError();
  }
  entry_ = std::move(lookup_result.value());
  if (!entry_.populated()) {
    performUpstreamRequest();
  } else {
    state_ = State::Exists;
    body_length_available_ = entry_.body_length_.value();
    handleValidationAndSendLookupResponses();
  }
}

void ActiveCacheEntry::performUpstreamRequest() {
  ENVOY_LOG(debug, "making upstream request to populate cache for {}", key_.path());
  mu_.AssertHeld();
  ASSERT(state_ == State::Pending);
  ASSERT(
      !lookup_subscribers_.empty(),
      "upstream request should only be possible if someone requested a lookup and it was a miss");
  ASSERT(!upstream_request_, "should only be one upstream request in flight");
  LookupSubscriber& last_sub = lookup_subscribers_.back();
  const ActiveLookupRequest& lookup = last_sub.context_->lookup();
  bool deranged = false;
  if (lookup.isRangeRequest()) {
    Http::RequestHeaderMapPtr deranged_headers =
        requestHeadersWithRangeRemoved(lookup.requestHeaders());
    upstream_request_ = lookup.upstreamRequestFactory().create(*deranged_headers);
    deranged = true;
  } else {
    upstream_request_ = lookup.upstreamRequestFactory().create(lookup.requestHeaders());
  }
  upstream_request_->getHeaders([this, p = shared_from_this(), deranged](
                                    Http::ResponseHeaderMapPtr headers, EndStream end_stream) {
    onUpstreamHeaders(std::move(headers), end_stream, deranged);
  });
}

void ActiveCacheEntry::onCacheWentAway() {
  mu_.AssertHeld();
  for (LookupSubscriber& sub : lookup_subscribers_) {
    auto result = upstreamPassThrough(sub.context_->movedLookup());
    sub.dispatcher().post([result = std::move(result), cb = std::move(sub.callback_)]() mutable {
      cb(std::move(result));
    });
  }
  lookup_subscribers_.clear();
}

void ActiveCacheEntry::processSuccessfulValidation(Http::ResponseHeaderMapPtr headers) {
  mu_.AssertHeld();
  ENVOY_LOG(debug, "successful validation");
  ASSERT(!lookup_subscribers_.empty(),
         "should be impossible to be validating with no context awaiting validation");

  const bool should_update_cached_entry =
      CacheHeadersUtils::shouldUpdateCachedEntry(*headers, *entry_.response_headers_);
  // Replace the 304 status code with the cached status code.
  headers->setStatus(entry_.response_headers_->getStatusValue());

  // Remove content length header if the 304 had one; if the cache entry had a
  // content length header it will be added by the header adding block below.
  headers->removeContentLength();

  // A response that has been validated should not contain an Age header as it is equivalent to a
  // freshly served response from the origin, unless the 304 response has an Age header, which
  // means it was served by an upstream cache.
  // Remove any existing Age header in the cached response.
  entry_.response_headers_->removeInline(CacheCustomHeaders::age());

  // Add any missing headers from the cached response to the 304 response.
  entry_.response_headers_->iterate([&headers](const Http::HeaderEntry& cached_header) {
    // TODO(yosrym93): see if we do this without copying the header key twice.
    Http::LowerCaseString key(cached_header.key().getStringView());
    if (headers->get(key).empty()) {
      headers->setCopy(key, cached_header.value().getStringView());
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  entry_.response_headers_ = std::move(headers);
  state_ = State::Exists;
  auto cache = cache_.lock();
  if (cache) {
    if (should_update_cached_entry) {
      // TODO(yosrym93): else evict, set state to Pending, and treat as insert.
      LookupSubscriber& sub = lookup_subscribers_.front();
      // Update metadata associated with the cached response. Right now this is only
      // response_time.
      entry_.response_metadata_.response_time_ = cache->time_source_.systemTime();
      cache->cache().updateHeaders(sub.dispatcher(), key_, *entry_.response_headers_,
                                   entry_.response_metadata_);
    }
  }

  CacheEntryStatus status = CacheEntryStatus::Validated;
  for (LookupSubscriber& recipient : lookup_subscribers_) {
    sendSuccessfulLookupResultTo(recipient, status);
    // For requests sharing the same validation upstream, use a distinct status
    // so it's detectable that we didn't need to do multiple validations.
    status = CacheEntryStatus::ValidatedFree;
  }
  lookup_subscribers_.clear();
}

void ActiveCacheEntry::onUpstreamHeaders(Http::ResponseHeaderMapPtr headers, EndStream end_stream,
                                         bool range_header_was_stripped) {
  absl::MutexLock lock(&mu_);
  ASSERT(upstream_request_);
  if (end_stream == EndStream::Reset) {
    upstream_request_ = nullptr;
    state_ = State::New;
    for (LookupSubscriber& subscriber : lookup_subscribers_) {
      subscriber.dispatcher().post([callback = std::move(subscriber.callback_)]() mutable {
        auto result = std::make_unique<ActiveLookupResult>();
        result->status_ = CacheEntryStatus::UpstreamReset;
        callback(std::move(result));
      });
    }
    lookup_subscribers_.clear();
    return;
  }
  ASSERT(headers);
  if (state_ == State::Validating) {
    if (Http::Utility::getResponseStatus(*headers) == enumToInt(Http::Code::NotModified)) {
      upstream_request_.reset();
      return processSuccessfulValidation(std::move(headers));
    } else {
      // Validate failed, so going down the 'insert' path instead.
      state_ = State::Pending;
    }
  } else {
    ASSERT(state_ == State::Pending, "should only get upstreamHeaders for Validating or Pending");
  }
  // If it turned out to be not cacheable, mark it as such, pass the already
  // open connection to the first request, and give any other requests in flight
  // a pass-through to upstream. (If the original request stripped off a range
  // header to populate the cache, we'll just drop that upstream request and
  // issue a new requests for each downstream.)
  absl::string_view cl = headers->getContentLengthValue();
  if (!cl.empty()) {
    absl::SimpleAtoi(cl, &content_length_header_) || (content_length_header_ = 0);
  }
  const VaryAllowList& vary_allow_list =
      lookup_subscribers_.front().context_->lookup().varyAllowList();
  if (!CacheabilityUtils::isCacheableResponse(*headers, vary_allow_list)) {
    state_ = State::NotCacheable;
    for (LookupSubscriber& sub : lookup_subscribers_) {
      sub.context_->setContentLength(content_length_header_);
      ActiveLookupResultPtr result;
      if (!range_header_was_stripped && &sub == &lookup_subscribers_.front()) {
        result = std::make_unique<ActiveLookupResult>();
        result->status_ = CacheEntryStatus::Uncacheable;
        result->http_source_ = std::make_unique<UpstreamRequestWithHeadersPrepopulated>(
            std::move(upstream_request_), std::move(headers), end_stream);
      } else {
        result = upstreamPassThrough(sub.context_->movedLookup());
      }
      sub.dispatcher().post([result = std::move(result), cb = std::move(sub.callback_)]() mutable {
        cb(std::move(result));
      });
    }
    lookup_subscribers_.clear();
    return;
  }
  auto active_cache = cache_.lock();
  if (!active_cache) {
    // Cache was deleted while callback was in flight. As a fallback just make all
    // requests pass through. This shouldn't happen, but it's possible that a config
    // update can come in *and* the last filter using the cache can get
    // downstream-disconnected and so deleted, leaving the upstream request
    // dangling with no cache to talk to.
    ENVOY_LOG(error, "cache config was deleted while upstream request was in flight");
    return onCacheWentAway();
  }
  if (end_stream == EndStream::End) {
    upstream_request_ = nullptr;
  }
  // Posted to ensure no deadlock on the mutex if callback is called directly.
  lookup_subscribers_.front().dispatcher().post(
      [p = shared_from_this(), &dispatcher = lookup_subscribers_.front().dispatcher(), key = key_,
       active_cache, headers = std::move(headers),
       upstream_request = std::move(upstream_request_)]() mutable {
        active_cache->cache().insert(dispatcher, key, std::move(headers),
                                     active_cache->makeMetadata(), std::move(upstream_request), p);
        // When the cache entry insertion completes it will call back to onHeadersInserted,
        // or on error onInsertFailed.
      });
}

void ActiveCacheImpl::lookup(ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb) {
  ASSERT(request);
  ASSERT(cb);
  std::shared_ptr<ActiveCacheEntry> entry = getEntry(request->key());
  entry->getLookupResult(*this, std::move(request), std::move(cb));
}

ResponseMetadata ActiveCacheImpl::makeMetadata() {
  ResponseMetadata metadata;
  metadata.response_time_ = time_source_.systemTime();
  return metadata;
}

void ActiveCacheEntry::performValidation() {
  mu_.AssertHeld();
  ENVOY_LOG(debug, "validating");
  state_ = State::Validating;
  ASSERT(!lookup_subscribers_.empty());
  LookupSubscriber& last_sub = lookup_subscribers_.front();
  const ActiveLookupRequest& lookup = last_sub.context_->lookup();
  Http::RequestHeaderMapPtr req = requestHeadersWithRangeRemoved(lookup.requestHeaders());
  CacheHeadersUtils::injectValidationHeaders(*req, *entry_.response_headers_);
  upstream_request_ = lookup.upstreamRequestFactory().create(*req);
  upstream_request_->getHeaders(
      [this, p = shared_from_this()](Http::ResponseHeaderMapPtr headers, EndStream end_stream) {
        onUpstreamHeaders(std::move(headers), end_stream, false);
      });
}

std::shared_ptr<ActiveCacheEntry> ActiveCacheImpl::getEntry(const Key& key) {
  const SystemTime now = time_source_.systemTime();
  cache().touch(key, now);
  absl::MutexLock lock(&mu_);
  auto [it, is_new] = entries_.try_emplace(key);
  if (is_new) {
    it->second = std::make_shared<ActiveCacheEntry>(weak_from_this(), key);
  }
  auto ret = it->second;
  ret->setExpiry(now + expiry_duration_);
  // As a lazy way of keeping the cache metadata from growing endlessly,
  // remove at most one adjacent metadata entry every time an entry is touched
  // if the adjacent entry hasn't been touched in a while.
  // This should do a decent job of expiring them simply, with a low cost, and
  // without taking any long-lived locks as would be required for periodic
  // scanning.
  if (++it == entries_.end()) {
    it = entries_.begin();
  }
  if (it->second->isExpiredAt(now)) {
    entries_.erase(it);
  }
  return ret;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
