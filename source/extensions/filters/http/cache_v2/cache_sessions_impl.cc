#include "source/extensions/filters/http/cache_v2/cache_sessions_impl.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache_v2/cache_custom_headers.h"
#include "source/extensions/filters/http/cache_v2/cache_entry_utils.h"
#include "source/extensions/filters/http/cache_v2/cache_headers_utils.h"
#include "source/extensions/filters/http/cache_v2/cacheability_utils.h"
#include "source/extensions/filters/http/cache_v2/range_utils.h"
#include "source/extensions/filters/http/cache_v2/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

using CancelWrapper::cancelWrapped;

class UpstreamRequestWithCacheabilityReset : public HttpSource {
public:
  UpstreamRequestWithCacheabilityReset(
      std::shared_ptr<const CacheableResponseChecker> cacheable_response_checker,
      std::unique_ptr<HttpSource> original_source, std::shared_ptr<CacheSession> entry)
      : cacheable_response_checker_(cacheable_response_checker),
        original_source_(std::move(original_source)), entry_(std::move(entry)) {}
  void getHeaders(GetHeadersCallback&& cb) override {
    original_source_->getHeaders(
        [entry = std::move(entry_), cb = std::move(cb),
         cacheable_response_checker = std::move(cacheable_response_checker_)](
            Http::ResponseHeaderMapPtr headers, EndStream end_stream) mutable {
          if (cacheable_response_checker->isCacheableResponse(*headers)) {
            entry->clearUncacheableState();
          }
          cb(std::move(headers), end_stream);
        });
  }
  void getBody(AdjustedByteRange range, GetBodyCallback&& cb) override {
    original_source_->getBody(std::move(range), std::move(cb));
  }
  void getTrailers(GetTrailersCallback&& cb) override {
    original_source_->getTrailers(std::move(cb));
  }

private:
  std::shared_ptr<const CacheableResponseChecker> cacheable_response_checker_;
  std::unique_ptr<HttpSource> original_source_;
  std::shared_ptr<CacheSession> entry_;
};

class UpstreamRequestWithHeadersPrepopulated : public HttpSource {
public:
  UpstreamRequestWithHeadersPrepopulated(std::unique_ptr<HttpSource> original_source,
                                         Http::ResponseHeaderMapPtr headers, EndStream end_stream)
      : original_source_(std::move(original_source)), headers_(std::move(headers)),
        end_stream_after_headers_(end_stream) {}
  void getHeaders(GetHeadersCallback&& cb) override {
    cb(std::move(headers_), end_stream_after_headers_);
  }
  void getBody(AdjustedByteRange range, GetBodyCallback&& cb) override {
    original_source_->getBody(std::move(range), std::move(cb));
  }
  void getTrailers(GetTrailersCallback&& cb) override {
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
          ASSERT(headers != nullptr, "it should be impossible for headers to be null");
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

std::shared_ptr<CacheSessions> CacheSessions::create(Server::Configuration::FactoryContext& context,
                                                     std::unique_ptr<HttpCache> cache) {
  return std::make_shared<CacheSessionsImpl>(context, std::move(cache));
}

CacheSession::CacheSession(std::weak_ptr<CacheSessionsImpl> cache_sessions, const Key& key)
    : cache_sessions_(std::move(cache_sessions)), key_(key) {}

void CacheSession::clearUncacheableState() {
  absl::MutexLock lock(mu_);
  if (state_ != State::NotCacheable) {
    return;
  }
  state_ = State::New;
}

void CacheSession::wantHeaders(Event::Dispatcher&, SystemTime lookup_timestamp,
                               GetHeadersCallback&& cb) {
  Http::ResponseHeaderMapPtr headers;
  EndStream end_stream_after_headers;
  {
    absl::MutexLock lock(mu_);
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

void CacheSession::wantBodyRange(AdjustedByteRange range, Event::Dispatcher& dispatcher,
                                 GetBodyCallback&& cb) {
  absl::MutexLock lock(mu_);
  ASSERT(entry_.response_headers_ != nullptr,
         "body should not be requested when headers haven't been sent");
  if (auto cache_sessions = cache_sessions_.lock()) {
    cache_sessions->stats().incCacheSessionsSubscribers();
  }
  body_subscribers_.emplace_back(dispatcher, std::move(range), std::move(cb));
  // if there's not already a body read operation in flight, start one.
  maybeTriggerBodyReadForWaitingSubscriber();
}

void CacheSession::wantTrailers(Event::Dispatcher& dispatcher, GetTrailersCallback&& cb) {
  absl::MutexLock lock(mu_);
  if (entry_.response_trailers_ != nullptr) {
    auto trailers = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*entry_.response_trailers_);
    dispatcher.post([cb = std::move(cb), trailers = std::move(trailers)]() mutable {
      cb(std::move(trailers), EndStream::End);
    });
    return;
  }
  ASSERT(!entry_.body_length_.has_value(),
         "wantTrailers should not be called when there are no trailers");
  if (auto cache_sessions = cache_sessions_.lock()) {
    cache_sessions->stats().incCacheSessionsSubscribers();
  }
  trailer_subscribers_.emplace_back(dispatcher, std::move(cb));
}

void CacheSession::onHeadersInserted(CacheReaderPtr cache_reader,
                                     Http::ResponseHeaderMapPtr headers, bool end_stream) {
  absl::MutexLock lock(mu_);
  std::shared_ptr<CacheSessionsImpl> cache_sessions = cache_sessions_.lock();
  if (!cache_sessions) {
    ENVOY_LOG(error, "cache config was deleted while header-insertion was in flight");
    return onCacheWentAway();
  }
  entry_.cache_reader_ = std::move(cache_reader);
  entry_.response_headers_ = std::move(headers);
  entry_.response_metadata_ = cache_sessions->makeMetadata();
  if (end_stream) {
    insertComplete();
  } else {
    state_ = State::Inserting;
  }
  sendLookupResponsesAndMaybeValidationRequest(CacheEntryStatus::Miss);
}

bool CacheSession::requiresValidationFor(const ActiveLookupRequest& lookup) const {
  mu_.AssertHeld();
  const Seconds age = CacheHeadersUtils::calculateAge(
      *entry_.response_headers_, entry_.response_metadata_.response_time_, lookup.timestamp());
  return lookup.requiresValidation(*entry_.response_headers_, age);
}

void CacheSession::sendLookupResponsesAndMaybeValidationRequest(CacheEntryStatus status) {
  mu_.AssertHeld();
  ASSERT(state_ == State::Exists || state_ == State::Inserting);
  auto it = lookup_subscribers_.begin();
  if (status != CacheEntryStatus::Miss) {
    // Reorder subscribers so those who do not require validation are at the end,
    // and 'it' is the first subscriber that does not require validation.
    it = std::partition(lookup_subscribers_.begin(), lookup_subscribers_.end(),
                        [this](LookupSubscriber& s) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
                          return requiresValidationFor(s.context_->lookup());
                        });
  }
  for (auto recipient = it; recipient != lookup_subscribers_.end(); recipient++) {
    sendSuccessfulLookupResultTo(*recipient, status);
    // If there was more than one recipient, and the first one was a miss, the
    // rest will be streamed.
    if (status == CacheEntryStatus::Miss) {
      status = CacheEntryStatus::Follower;
    }
  }
  if (it != lookup_subscribers_.end()) {
    if (auto cache_sessions = cache_sessions_.lock()) {
      cache_sessions->stats().subCacheSessionsSubscribers(
          std::distance(it, lookup_subscribers_.end()));
    }
  }
  lookup_subscribers_.erase(it, lookup_subscribers_.end());
  if (!lookup_subscribers_.empty()) {
    // At least one subscriber required validation.
    return performValidation();
  }
}

EndStream CacheSession::endStreamAfterHeaders() const {
  mu_.AssertHeld();
  bool end_stream = entry_.body_length_.value_or(1) == 0 && entry_.response_trailers_ == nullptr;
  return end_stream ? EndStream::End : EndStream::More;
}

EndStream CacheSession::endStreamAfterBody() const {
  mu_.AssertHeld();
  ASSERT(entry_.body_length_.has_value(),
         "should not be testing endStreamAfterBody if body not complete");
  return (entry_.response_trailers_ == nullptr) ? EndStream::End : EndStream::More;
}

void CacheSession::sendSuccessfulLookupResultTo(LookupSubscriber& subscriber,
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

void CacheSession::onBodyInserted(AdjustedByteRange range, bool end_stream) {
  absl::MutexLock lock(mu_);
  body_length_available_ = range.end();
  if (end_stream) {
    insertComplete();
    ASSERT(trailer_subscribers_.empty(), "should not be trailer requests before body was complete");
  }
  maybeTriggerBodyReadForWaitingSubscriber();
}

void CacheSession::onTrailersInserted(Http::ResponseTrailerMapPtr trailers) {
  ASSERT(trailers);
  absl::MutexLock lock(mu_);
  entry_.response_trailers_ = std::move(trailers);
  insertComplete();
  for (TrailerSubscriber& subscriber : trailer_subscribers_) {
    sendTrailersTo(subscriber);
  }
  if (auto cache_sessions = cache_sessions_.lock()) {
    cache_sessions->stats().subCacheSessionsSubscribers(trailer_subscribers_.size());
  }
  trailer_subscribers_.clear();
  // If there's a body subscriber waiting for more body that doesn't exist,
  // it needs to be notified so it can call getTrailers.
  abortBodyOutOfRangeSubscribers();
}

void CacheSession::sendTrailersTo(TrailerSubscriber& subscriber) {
  mu_.AssertHeld();
  ASSERT(entry_.response_trailers_ != nullptr);
  subscriber.dispatcher().post(
      [trailers = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(*entry_.response_trailers_),
       callback = std::move(subscriber.callback_)]() mutable {
        callback(std::move(trailers), EndStream::End);
      });
}

void CacheSession::onInsertFailed(absl::Status status) {
  absl::MutexLock lock(mu_);
  ENVOY_LOG(error, "cache insert failed: {}", status);
  onCacheError();
}

static void postUpstreamPassThrough(CacheSession::LookupSubscriber&& sub, CacheEntryStatus status) {
  Event::Dispatcher& dispatcher = sub.dispatcher();
  dispatcher.post([sub = std::move(sub), status]() mutable {
    auto result = std::make_unique<ActiveLookupResult>();
    auto upstream = sub.context_->lookup().createUpstreamRequest();
    upstream->sendHeaders(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(sub.context_->lookup().requestHeaders()));
    result->http_source_ = std::move(upstream);
    result->status_ = status;
    sub.callback_(std::move(result));
  });
}

static void postUpstreamPassThroughWithReset(CacheSession::LookupSubscriber&& sub,
                                             std::shared_ptr<CacheSession> entry) {
  Event::Dispatcher& dispatcher = sub.dispatcher();
  dispatcher.post([sub = std::move(sub), entry = std::move(entry)]() mutable {
    auto result = std::make_unique<ActiveLookupResult>();
    auto upstream = sub.context_->lookup().createUpstreamRequest();
    upstream->sendHeaders(
        Http::createHeaderMap<Http::RequestHeaderMapImpl>(sub.context_->lookup().requestHeaders()));
    result->http_source_ = std::make_unique<UpstreamRequestWithCacheabilityReset>(
        sub.context_->lookup().cacheableResponseChecker(), std::move(upstream), entry);
    result->status_ = CacheEntryStatus::Uncacheable;
    sub.callback_(std::move(result));
  });
}

void CacheSession::onCacheError() {
  mu_.AssertHeld();
  auto cache_sessions = cache_sessions_.lock();
  if (cache_sessions) {
    Event::Dispatcher* dispatcher = nullptr;
    if (!lookup_subscribers_.empty()) {
      dispatcher = &lookup_subscribers_.front().dispatcher();
    } else if (!body_subscribers_.empty()) {
      dispatcher = &body_subscribers_.front().dispatcher();
    } else if (!trailer_subscribers_.empty()) {
      dispatcher = &trailer_subscribers_.front().dispatcher();
    }
    if (dispatcher) {
      // TODO(toddmgreer): there may be some kinds of cache error that
      // don't merit evicting the entry.
      cache_sessions->cache().evict(*dispatcher, key_);
    }
    cache_sessions->stats().subCacheSessionsSubscribers(body_subscribers_.size());
    cache_sessions->stats().subCacheSessionsSubscribers(trailer_subscribers_.size());
    cache_sessions->stats().subCacheSessionsSubscribers(lookup_subscribers_.size());
  }
  for (LookupSubscriber& sub : lookup_subscribers_) {
    postUpstreamPassThrough(std::move(sub), CacheEntryStatus::LookupError);
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
  state_ = State::New;
}

void CacheSession::insertComplete() {
  mu_.AssertHeld();
  state_ = State::Exists;
  entry_.body_length_ = body_length_available_;
  if (content_length_header_ == entry_.body_length_) {
    return;
  }
  if (content_length_header_ != 0) {
    ENVOY_LOG(error,
              "cache insert for {}{} had content-length header {} but actual size {}. Cache has "
              "modified the header to match actual size.",
              key_.host(), key_.path(), content_length_header_, entry_.body_length_.value());
  }
  content_length_header_ = body_length_available_;
}

void CacheSession::abortBodyOutOfRangeSubscribers() {
  mu_.AssertHeld();
  if (!entry_.body_length_.has_value()) {
    // Don't know if a request is out of range until the available range is known.
    return;
  }
  // For any subscribers whose requested range has been revealed to be invalid
  // (we only get here in the case where content length was specified in the
  // headers, but the actual body was shorter, i.e. the upstream response was
  // actually invalid), reset their requests.
  // Subscribers who asked for body starting at or beyond the end of the
  // real size receive null body rather than reset.
  EndStream end_stream = endStreamAfterBody();
  auto cache_sessions = cache_sessions_.lock();
  body_subscribers_.erase(
      std::remove_if(body_subscribers_.begin(), body_subscribers_.end(),
                     [this, end_stream, &cache_sessions](BodySubscriber& bs)
                         ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
                           if (bs.range_.begin() >= body_length_available_) {
                             if (bs.range_.begin() == body_length_available_) {
                               auto cb = std::move(bs.callback_);
                               bs.dispatcher().post([cb = std::move(cb), end_stream]() mutable {
                                 cb(nullptr, end_stream);
                               });
                             } else {
                               bs.callback_(nullptr, EndStream::Reset);
                             }
                             if (cache_sessions) {
                               cache_sessions->stats().subCacheSessionsSubscribers(1);
                             }
                             return true;
                           }
                           return false;
                         }),
      body_subscribers_.end());
}

void CacheSession::maybeTriggerBodyReadForWaitingSubscriber() {
  mu_.AssertHeld();
  ASSERT(entry_.cache_reader_);
  if (read_action_in_flight_) {
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
  // CacheSession alive. We post to a thread before making the request for two reasons - we want
  // the request to be performed on the requester's worker thread for balance, and we want to be
  // able to lock the mutex again on the callback - if the cache called back immediately rather than
  // posting and we *didn't* post before making the request, the mutex would still be held
  // from this outer function so the callback would deadlock. By posting to a queue we ensure
  // that deadlock cannot occur.
  // Also, by ensuring the action occurs from a dispatcher queue, we guarantee that
  // the "trigger again" at the end of onBodyChunkFromCache can't build up to a stack overflow
  // of maybeTrigger->getBody->onBodyChunk->maybeTrigger->...
  read_action_in_flight_ = true;
  it->dispatcher().post([&dispatcher = it->dispatcher(), p = shared_from_this(), range,
                         cache_reader = entry_.cache_reader_.get()]() mutable {
    cache_reader->getBody(
        dispatcher, range,
        [p = std::move(p), range](Buffer::InstancePtr buffer, EndStream end_stream) {
          p->onBodyChunkFromCache(std::move(range), std::move(buffer), end_stream);
        });
  });
}

bool CacheSession::canReadBodyRangeFromCacheEntry(BodySubscriber& subscriber) {
  mu_.AssertHeld();
  return subscriber.range_.begin() < body_length_available_;
}

void CacheSession::onBodyChunkFromCache(AdjustedByteRange range, Buffer::InstancePtr buffer,
                                        EndStream end_stream) {
  absl::MutexLock lock(mu_);
  read_action_in_flight_ = false;
  if (end_stream == EndStream::Reset) {
    ENVOY_LOG(error, "cache entry provoked reset");
    onCacheError();
    return;
  }
  if (buffer == nullptr) {
    IS_ENVOY_BUG("cache returned null buffer non-reset");
    onCacheError();
    return;
  }
  ASSERT(buffer->length() <= range.length());
  if (buffer->length() < range.length()) {
    range = AdjustedByteRange(range.begin(), range.begin() + buffer->length());
  }
  auto recipients_begin = std::partition(body_subscribers_.begin(), body_subscribers_.end(),
                                         [&range](BodySubscriber& subscriber) {
                                           return subscriber.range_.begin() < range.begin() ||
                                                  subscriber.range_.begin() >= range.end();
                                         });
  ASSERT(recipients_begin != body_subscribers_.end(),
         "reading body chunk from cache with no corresponding request shouldn't happen");
  if (std::next(recipients_begin) == body_subscribers_.end()) {
    BodySubscriber& subscriber = *recipients_begin;
    ASSERT(subscriber.range_.begin() == range.begin(),
           "if there's only one matching subscriber it should have requested this precise chunk");
    // There is only one recipient of this chunk, send it the actual buffer,
    // no need to copy.
    sendBodyChunkTo(subscriber,
                    AdjustedByteRange(subscriber.range_.begin(),
                                      std::min(subscriber.range_.end(), range.end())),
                    std::move(buffer));
  } else {
    uint8_t* bytes = static_cast<uint8_t*>(buffer->linearize(range.length()));
    for (auto it = recipients_begin; it != body_subscribers_.end(); it++) {
      AdjustedByteRange r(it->range_.begin(), std::min(it->range_.end(), range.end()));
      sendBodyChunkTo(
          *it, r,
          std::make_unique<Buffer::OwnedImpl>(bytes + r.begin() - range.begin(), r.length()));
    }
  }
  if (auto cache_sessions = cache_sessions_.lock()) {
    cache_sessions->stats().subCacheSessionsSubscribers(
        std::distance(recipients_begin, body_subscribers_.end()));
  }
  body_subscribers_.erase(recipients_begin, body_subscribers_.end());
  maybeTriggerBodyReadForWaitingSubscriber();
}

void CacheSession::sendBodyChunkTo(BodySubscriber& subscriber, AdjustedByteRange range,
                                   Buffer::InstancePtr buffer) {
  mu_.AssertHeld();
  bool end_stream = entry_.body_length_.has_value() && range.end() == entry_.body_length_.value() &&
                    entry_.response_trailers_ == nullptr;
  subscriber.dispatcher().post([end_stream, callback = std::move(subscriber.callback_),
                                buffer = std::move(buffer)]() mutable {
    callback(std::move(buffer), end_stream ? EndStream::End : EndStream::More);
  });
}

CacheSession::~CacheSession() { ASSERT(!upstream_request_); }

void CacheSession::getLookupResult(ActiveLookupRequestPtr lookup, ActiveLookupResultCallback&& cb) {
  ASSERT(lookup->dispatcher().isThreadSafe());
  absl::MutexLock lock(mu_);
  LookupSubscriber sub{std::make_unique<ActiveLookupContext>(std::move(lookup), shared_from_this(),
                                                             content_length_header_),
                       std::move(cb)};
  switch (state_) {
  case State::Vary:
    IS_ENVOY_BUG("not implemented yet");
    ABSL_FALLTHROUGH_INTENDED;
  case State::NotCacheable: {
    postUpstreamPassThroughWithReset(std::move(sub), shared_from_this());
    return;
  }
  case State::Validating:
  case State::Pending:
    sub.context_->lookup().stats().incCacheSessionsSubscribers();
    lookup_subscribers_.push_back(std::move(sub));
    return;
  case State::Exists:
  case State::Inserting: {
    CacheEntryStatus status = CacheEntryStatus::Hit;
    if (requiresValidationFor(sub.context_->lookup())) {
      if (sub.context_->lookup().requestHeaders().getMethodValue() ==
          Http::Headers::get().MethodValues.Head) {
        // A HEAD request that requires validation can't write to the
        // cache or use the cache entry, so just turn it into a pass-through.
        return postUpstreamPassThrough(std::move(sub), CacheEntryStatus::Uncacheable);
      }
      if (state_ == State::Inserting) {
        // Skip validation if the cache write is still in progress.
        status = CacheEntryStatus::ValidatedFree;
      } else {
        sub.context_->lookup().stats().incCacheSessionsSubscribers();
        lookup_subscribers_.push_back(std::move(sub));
        return performValidation();
      }
    }
    auto result = std::make_unique<ActiveLookupResult>();
    Event::Dispatcher& dispatcher = sub.dispatcher();
    result->http_source_ = std::move(sub.context_);
    result->status_ = status;
    dispatcher.post([cb = std::move(sub.callback_), result = std::move(result)]() mutable {
      cb(std::move(result));
    });
    return;
  }
  case State::New: {
    Event::Dispatcher& dispatcher = sub.dispatcher();
    if (sub.context_->lookup().requestHeaders().getMethodValue() ==
        Http::Headers::get().MethodValues.Head) {
      // HEAD requests are not cacheable, just pass through.
      postUpstreamPassThrough(std::move(sub), CacheEntryStatus::Uncacheable);
      return;
    }
    LookupRequest request(Key{sub.context_->lookup().key()}, dispatcher);
    sub.context_->lookup().stats().incCacheSessionsSubscribers();
    lookup_subscribers_.emplace_back(std::move(sub));
    state_ = State::Pending;
    std::shared_ptr<CacheSessionsImpl> cache_sessions = cache_sessions_.lock();
    ASSERT(cache_sessions, "should be impossible for cache to be deleted in getLookupResult");
    // posted to prevent callback mutex-deadlock.
    return dispatcher.post([cache_sessions = std::move(cache_sessions), p = shared_from_this(),
                            request = std::move(request)]() mutable {
      // p is captured as shared_ptr to ensure 'this' is not deleted while the
      // lookup is in flight.
      cache_sessions->cache().lookup(
          std::move(request), [p = std::move(p)](absl::StatusOr<LookupResult>&& lookup_result) {
            p->onCacheLookupResult(std::move(lookup_result));
          });
    });
  }
  }
}

void CacheSession::onCacheLookupResult(absl::StatusOr<LookupResult>&& lookup_result) {
  absl::MutexLock lock(mu_);
  if (!lookup_result.ok()) {
    return onCacheError();
  }
  entry_ = std::move(lookup_result.value());
  if (!entry_.populated()) {
    performUpstreamRequest();
  } else {
    state_ = State::Exists;
    body_length_available_ = entry_.body_length_.value();
    sendLookupResponsesAndMaybeValidationRequest();
  }
}

void CacheSession::performUpstreamRequest() {
  ENVOY_LOG(debug, "making upstream request to populate cache for {}", key_.path());
  mu_.AssertHeld();
  ASSERT(state_ == State::Pending);
  ASSERT(
      !lookup_subscribers_.empty(),
      "upstream request should only be possible if someone requested a lookup and it was a miss");
  ASSERT(!upstream_request_, "should only be one upstream request in flight");
  LookupSubscriber& first_sub = lookup_subscribers_.front();
  const ActiveLookupRequest& lookup = first_sub.context_->lookup();
  Http::RequestHeaderMapPtr request_headers;
  bool was_ranged_request = lookup.isRangeRequest();
  if (was_ranged_request) {
    request_headers = requestHeadersWithRangeRemoved(lookup.requestHeaders());
  } else {
    request_headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>(lookup.requestHeaders());
  }
  upstream_request_ = lookup.createUpstreamRequest();
  first_sub.dispatcher().post([upstream_request = upstream_request_.get(),
                               request_headers = std::move(request_headers), this,
                               p = shared_from_this(), was_ranged_request]() mutable {
    upstream_request->sendHeaders(std::move(request_headers));
    upstream_request->getHeaders([this, p = std::move(p), was_ranged_request](
                                     Http::ResponseHeaderMapPtr headers, EndStream end_stream) {
      onUpstreamHeaders(std::move(headers), end_stream, was_ranged_request);
    });
  });
}

void CacheSession::onCacheWentAway() {
  mu_.AssertHeld();
  for (LookupSubscriber& sub : lookup_subscribers_) {
    postUpstreamPassThrough(std::move(sub), CacheEntryStatus::LookupError);
  }
  lookup_subscribers_.clear();
}

void CacheSession::processSuccessfulValidation(Http::ResponseHeaderMapPtr headers) {
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
  if (auto cache_sessions = cache_sessions_.lock()) {
    if (should_update_cached_entry) {
      // TODO(yosrym93): else evict, set state to Pending, and treat as insert.
      LookupSubscriber& sub = lookup_subscribers_.front();
      // Update metadata associated with the cached response. Right now this is only
      // response_time.
      entry_.response_metadata_.response_time_ = cache_sessions->time_source_.systemTime();
      cache_sessions->cache().updateHeaders(sub.dispatcher(), key_, *entry_.response_headers_,
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
  if (auto cache_sessions = cache_sessions_.lock()) {
    cache_sessions->stats().subCacheSessionsSubscribers(lookup_subscribers_.size());
  }
  lookup_subscribers_.clear();
}

void CacheSession::onUncacheable(Http::ResponseHeaderMapPtr headers, EndStream end_stream,
                                 bool range_header_was_stripped) {
  // If it turned out to be not cacheable, mark it as such, pass the already
  // open connection to the first request, and give any other requests in flight
  // a pass-through to upstream.
  // If the upstream request stripped off a range header from the downstream
  // request in order to populate the cache, we'll have to drop that upstream
  // request and just issue a new request for every downstream.
  mu_.AssertHeld();
  state_ = State::NotCacheable;
  bool use_existing_stream = !range_header_was_stripped;
  if (!use_existing_stream) {
    // Reset the upstream request if the request wanted a range and
    // the upstream request didn't want a range.
    upstream_request_ = nullptr;
  }
  for (LookupSubscriber& sub : lookup_subscribers_) {
    sub.context_->setContentLength(content_length_header_);
    if (use_existing_stream) {
      ActiveLookupResultPtr result = std::make_unique<ActiveLookupResult>();
      result->status_ = CacheEntryStatus::Uncacheable;
      result->http_source_ = std::make_unique<UpstreamRequestWithHeadersPrepopulated>(
          std::move(upstream_request_), std::move(headers), end_stream);
      sub.dispatcher().post([result = std::move(result), cb = std::move(sub.callback_)]() mutable {
        cb(std::move(result));
      });
      use_existing_stream = false;
    } else {
      postUpstreamPassThrough(std::move(sub), CacheEntryStatus::Uncacheable);
    }
  }
  if (auto cache_sessions = cache_sessions_.lock()) {
    cache_sessions->stats().subCacheSessionsSubscribers(lookup_subscribers_.size());
  }
  lookup_subscribers_.clear();
  return;
}

void CacheSession::onUpstreamHeaders(Http::ResponseHeaderMapPtr headers, EndStream end_stream,
                                     bool range_header_was_stripped) {
  absl::MutexLock lock(mu_);
  Event::Dispatcher& dispatcher = lookup_subscribers_.front().dispatcher();
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
    if (auto cache_sessions = cache_sessions_.lock()) {
      cache_sessions->stats().subCacheSessionsSubscribers(lookup_subscribers_.size());
    }
    lookup_subscribers_.clear();
    return;
  }
  ASSERT(headers);
  if (state_ == State::Validating) {
    if (Http::Utility::getResponseStatus(*headers) == enumToInt(Http::Code::NotModified)) {
      upstream_request_ = nullptr;
      return processSuccessfulValidation(std::move(headers));
    } else {
      // Validate failed, so going down the 'insert' path instead.
      state_ = State::Pending;
      if (auto cache_sessions = cache_sessions_.lock()) {
        cache_sessions->cache().evict(dispatcher, key_);
      }
      body_length_available_ = 0;
      entry_ = {};
    }
  } else {
    ASSERT(state_ == State::Pending, "should only get upstreamHeaders for Validating or Pending");
  }
  absl::string_view cl = headers->getContentLengthValue();
  if (!cl.empty()) {
    absl::SimpleAtoi(cl, &content_length_header_) || (content_length_header_ = 0);
  }
  if (!lookup_subscribers_.front().context_->lookup().isCacheableResponse(*headers)) {
    return onUncacheable(std::move(headers), end_stream, range_header_was_stripped);
  }
  if (VaryHeaderUtils::hasVary(*headers)) {
    // TODO(ravenblack): implement Vary header support.
    ENVOY_LOG(debug, "Vary header found in upstream response, treating as not cacheable");
    return onUncacheable(std::move(headers), end_stream, range_header_was_stripped);
  }
  auto cache_sessions = cache_sessions_.lock();
  if (!cache_sessions) {
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
  // We're already on this subscriber's thread; this is posted to ensure no
  // deadlock on the mutex if the insert operation calls back directly.
  lookup_subscribers_.front().dispatcher().post(
      [p = shared_from_this(), &dispatcher = lookup_subscribers_.front().dispatcher(), key = key_,
       cache_sessions, headers = std::move(headers),
       upstream_request = std::move(upstream_request_)]() mutable {
        cache_sessions->cache().insert(dispatcher, key, std::move(headers),
                                       cache_sessions->makeMetadata(), std::move(upstream_request),
                                       p);
        // When the cache entry insertion completes it will call back to onHeadersInserted,
        // or on error onInsertFailed.
      });
}

void CacheSessionsImpl::lookup(ActiveLookupRequestPtr request, ActiveLookupResultCallback&& cb) {
  ASSERT(request);
  ASSERT(cb);
  std::shared_ptr<CacheSession> entry = getEntry(request->key());
  entry->getLookupResult(std::move(request), std::move(cb));
}

ResponseMetadata CacheSessionsImpl::makeMetadata() {
  ResponseMetadata metadata;
  metadata.response_time_ = time_source_.systemTime();
  return metadata;
}

void CacheSession::performValidation() {
  mu_.AssertHeld();
  ASSERT(!lookup_subscribers_.empty());
  ENVOY_LOG(debug, "validating");
  state_ = State::Validating;
  LookupSubscriber& first_sub = lookup_subscribers_.front();
  const ActiveLookupRequest& lookup = first_sub.context_->lookup();
  Http::RequestHeaderMapPtr req = requestHeadersWithRangeRemoved(lookup.requestHeaders());
  CacheHeadersUtils::injectValidationHeaders(*req, *entry_.response_headers_);
  upstream_request_ = lookup.createUpstreamRequest();
  first_sub.dispatcher().post([upstream_request = upstream_request_.get(), req = std::move(req),
                               this, p = shared_from_this()]() mutable {
    upstream_request->sendHeaders(std::move(req));
    upstream_request->getHeaders(
        [this, p = std::move(p)](Http::ResponseHeaderMapPtr headers, EndStream end_stream) {
          onUpstreamHeaders(std::move(headers), end_stream, false);
        });
  });
}

std::shared_ptr<CacheSession> CacheSessionsImpl::getEntry(const Key& key) {
  const SystemTime now = time_source_.systemTime();
  cache().touch(key, now);
  absl::MutexLock lock(mu_);
  auto [it, is_new] = entries_.try_emplace(key);
  if (is_new) {
    stats().incCacheSessionsEntries();
    it->second = std::make_shared<CacheSession>(weak_from_this(), key);
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
    stats().decCacheSessionsEntries();
    entries_.erase(it);
  }
  return ret;
}

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
