#include "source/extensions/filters/http/cache/cache_filter.h"

#include "envoy/http/header_map.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/cache/cache_custom_headers.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_filter_logging_info.h"
#include "source/extensions/filters/http/cache/cacheability_utils.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {
inline bool isResponseNotModified(const Http::ResponseHeaderMap& response_headers) {
  return Http::Utility::getResponseStatus(response_headers) == enumToInt(Http::Code::NotModified);
}
} // namespace

struct CacheResponseCodeDetailValues {
  const absl::string_view ResponseFromCacheFilter = "cache.response_from_cache_filter";
};

using CacheResponseCodeDetails = ConstSingleton<CacheResponseCodeDetailValues>;

CacheFilter::CacheFilter(const envoy::extensions::filters::http::cache::v3::CacheConfig& config,
                         const std::string&, Stats::Scope&, TimeSource& time_source,
                         std::shared_ptr<HttpCache> http_cache)
    : time_source_(time_source), cache_(http_cache),
      vary_allow_list_(config.allowed_vary_headers()) {}

void CacheFilter::onDestroy() {
  filter_state_ = FilterState::Destroyed;
  if (lookup_ != nullptr) {
    lookup_->onDestroy();
  }
  if (insert_queue_ != nullptr) {
    // The filter can complete and be destroyed while there is still data being
    // written to the cache. In this case the filter hands ownership of the
    // queue to itself, which cancels all the callbacks to the filter, but allows
    // the queue to complete any write operations before deleting itself.
    //
    // In the case that the queue is already empty, or in a state which cannot
    // complete, setSelfOwned will provoke the queue to abort the write operation.
    insert_queue_->setSelfOwned(std::move(insert_queue_));
    insert_queue_.reset();
  }
}

void CacheFilter::onStreamComplete() {
  LookupStatus lookup_status = lookupStatus();
  InsertStatus insert_status = insertStatus();
  decoder_callbacks_->streamInfo().filterState()->setData(
      CacheFilterLoggingInfo::FilterStateKey,
      std::make_shared<CacheFilterLoggingInfo>(lookup_status, insert_status),
      StreamInfo::FilterState::StateType::ReadOnly);
}

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  if (!cache_) {
    filter_state_ = FilterState::NotServingFromCache;
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders: {}", *decoder_callbacks_, headers);
  if (!end_stream) {
    ENVOY_STREAM_LOG(
        debug,
        "CacheFilter::decodeHeaders ignoring request because it has body and/or trailers: {}",
        *decoder_callbacks_, headers);
    filter_state_ = FilterState::NotServingFromCache;
    return Http::FilterHeadersStatus::Continue;
  }
  if (!CacheabilityUtils::canServeRequestFromCache(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders ignoring uncacheable request: {}",
                     *decoder_callbacks_, headers);
    filter_state_ = FilterState::NotServingFromCache;
    insert_status_ = InsertStatus::NoInsertRequestNotCacheable;
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);

  LookupRequest lookup_request(headers, time_source_.systemTime(), vary_allow_list_);
  request_allows_inserts_ = !lookup_request.requestCacheControl().no_store_;
  is_head_request_ = headers.getMethodValue() == Http::Headers::get().MethodValues.Head;
  lookup_ = cache_->makeLookupContext(std::move(lookup_request), *decoder_callbacks_);

  ASSERT(lookup_);
  getHeaders(headers);
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders starting lookup", *decoder_callbacks_);

  // Stop the decoding stream until the cache lookup result is ready.
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) {
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    // This call was invoked during decoding by decoder_callbacks_->encodeHeaders because a fresh
    // cached response was found and is being added to the encoding stream -- ignore it.
    return Http::FilterHeadersStatus::Continue;
  }

  // If lookup_ is null, the request wasn't cacheable, so the response isn't either.
  if (!lookup_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (lookup_result_ == nullptr) {
    // Filter chain iteration is paused while a lookup is outstanding, but the filter chain manager
    // can still generate a local reply. One case where this can happen is when a downstream idle
    // timeout fires, which may mean that the HttpCache isn't correctly setting deadlines on its
    // asynchronous operations or is otherwise getting stuck.
    ENVOY_BUG(Http::Utility::getResponseStatus(headers) !=
                  Envoy::enumToInt(Http::Code::RequestTimeout),
              "Request timed out while cache lookup was outstanding.");
    filter_state_ = FilterState::NotServingFromCache;
    return Http::FilterHeadersStatus::Continue;
  }

  if (filter_state_ == FilterState::ValidatingCachedResponse && isResponseNotModified(headers)) {
    processSuccessfulValidation(headers);
    // Stop the encoding stream until the cached response is fetched & added to the encoding stream.
    if (is_head_request_) {
      // Return since HEAD requests are not cached
      return Http::FilterHeadersStatus::Continue;
    } else {
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  // Either a cache miss or a cache entry that is no longer valid.
  // Check if the new response can be cached.
  if (request_allows_inserts_ && !is_head_request_ &&
      CacheabilityUtils::isCacheableResponse(headers, vary_allow_list_)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting headers", *encoder_callbacks_);
    auto insert_context = cache_->makeInsertContext(std::move(lookup_), *encoder_callbacks_);
    if (insert_context != nullptr) {
      // The callbacks passed to CacheInsertQueue are all called through the dispatcher,
      // so they're thread-safe. During CacheFilter::onDestroy the queue is given ownership
      // of itself and all the callbacks are cancelled, so they are also filter-destruction-safe.
      insert_queue_ =
          std::make_unique<CacheInsertQueue>(cache_, *encoder_callbacks_, std::move(insert_context),
                                             // Cache aborted callback.
                                             [this]() {
                                               insert_queue_ = nullptr;
                                               insert_status_ = InsertStatus::InsertAbortedByCache;
                                             });
      // Add metadata associated with the cached response. Right now this is only response_time;
      const ResponseMetadata metadata = {time_source_.systemTime()};
      insert_queue_->insertHeaders(headers, metadata, end_stream);
    }
    if (end_stream) {
      insert_status_ = InsertStatus::InsertSucceeded;
    }
    // insert_status_ remains absl::nullopt if end_stream == false, as we have not completed the
    // insertion yet.
  } else {
    insert_status_ = InsertStatus::NoInsertResponseNotCacheable;
  }
  filter_state_ = FilterState::NotServingFromCache;
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    // This call was invoked during decoding by decoder_callbacks_->encodeData because a fresh
    // cached response was found and is being added to the encoding stream -- ignore it.
    return Http::FilterDataStatus::Continue;
  }
  if (filter_state_ == FilterState::EncodeServingFromCache) {
    // Stop the encoding stream until the cached response is fetched & added to the encoding stream.
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
  if (insert_queue_ != nullptr) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeData inserting body", *encoder_callbacks_);
    insert_queue_->insertBody(data, end_stream);
    if (end_stream) {
      // We don't actually know if the insert succeeded, but as far as the
      // filter is concerned it has been fully handed off to the cache
      // implementation.
      insert_status_ = InsertStatus::InsertSucceeded;
    }
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus CacheFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    // This call was invoked during decoding by decoder_callbacks_->encodeTrailers because a fresh
    // cached response was found and is being added to the encoding stream -- ignore it.
    return Http::FilterTrailersStatus::Continue;
  }
  if (filter_state_ == FilterState::EncodeServingFromCache) {
    // Stop the encoding stream until the cached response is fetched & added to the encoding stream.
    return Http::FilterTrailersStatus::StopIteration;
  }
  response_has_trailers_ = !trailers.empty();
  if (insert_queue_ != nullptr) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeTrailers inserting trailers", *encoder_callbacks_);
    insert_queue_->insertTrailers(trailers);
  }
  insert_status_ = InsertStatus::InsertSucceeded;

  return Http::FilterTrailersStatus::Continue;
}

/*static*/ LookupStatus
CacheFilter::resolveLookupStatus(absl::optional<CacheEntryStatus> cache_entry_status,
                                 FilterState filter_state) {
  if (cache_entry_status.has_value()) {
    switch (cache_entry_status.value()) {
    case CacheEntryStatus::Ok:
      return LookupStatus::CacheHit;
    case CacheEntryStatus::Unusable:
      return LookupStatus::CacheMiss;
    case CacheEntryStatus::RequiresValidation: {
      // The CacheFilter sent the response upstream for validation; check the
      // filter state to see whether and how the upstream responded. The
      // filter currently won't send the stale entry if it can't reach the
      // upstream or if the upstream responds with a 5xx, so don't include
      // special handling for those cases.
      switch (filter_state) {
      case FilterState::ValidatingCachedResponse:
        return LookupStatus::RequestIncomplete;
      case FilterState::EncodeServingFromCache:
        ABSL_FALLTHROUGH_INTENDED;
      case FilterState::ResponseServedFromCache:
        // Functionally a cache hit, this is differentiated for metrics reporting.
        return LookupStatus::StaleHitWithSuccessfulValidation;
      case FilterState::NotServingFromCache:
        return LookupStatus::StaleHitWithFailedValidation;
      case FilterState::Initial:
        ABSL_FALLTHROUGH_INTENDED;
      case FilterState::DecodeServingFromCache:
        ABSL_FALLTHROUGH_INTENDED;
      case FilterState::Destroyed:
        IS_ENVOY_BUG(absl::StrCat("Unexpected filter state in requestCacheStatus: cache lookup "
                                  "response required validation, but filter state is ",
                                  filter_state));
      }
      return LookupStatus::Unknown;
    }
    case CacheEntryStatus::FoundNotModified:
      // TODO(capoferro): Report this as a FoundNotModified when we handle
      // those.
      return LookupStatus::CacheHit;
    case CacheEntryStatus::LookupError:
      return LookupStatus::LookupError;
    }
    IS_ENVOY_BUG(absl::StrCat(
        "Unhandled CacheEntryStatus encountered when retrieving request cache status: " +
        std::to_string(static_cast<int>(filter_state))));
    return LookupStatus::Unknown;
  }
  // Either decodeHeaders decided not to do a cache lookup (because the
  // request isn't cacheable), or decodeHeaders hasn't been called yet.
  switch (filter_state) {
  case FilterState::Initial:
    return LookupStatus::RequestIncomplete;
  case FilterState::NotServingFromCache:
    return LookupStatus::RequestNotCacheable;
  // Ignore the following lines. This code should not be executed.
  // GCOV_EXCL_START
  case FilterState::ValidatingCachedResponse:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::DecodeServingFromCache:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::EncodeServingFromCache:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::ResponseServedFromCache:
    ABSL_FALLTHROUGH_INTENDED;
  case FilterState::Destroyed:
    ENVOY_LOG(error, absl::StrCat("Unexpected filter state in requestCacheStatus: "
                                  "lookup_result_ is empty but filter state is ",
                                  filter_state));
  }
  return LookupStatus::Unknown;
}

void CacheFilter::getHeaders(Http::RequestHeaderMap& request_headers) {
  ASSERT(lookup_, "CacheFilter is trying to call getHeaders with no LookupContext");

  // If the cache posts a callback to the dispatcher then the CacheFilter is destroyed for any
  // reason (e.g client disconnected and HTTP stream terminated), then there is no guarantee that
  // the posted callback will run before the filter is deleted. Hence, a weak_ptr to the CacheFilter
  // is captured and used to make sure the CacheFilter is still alive before accessing it in the
  // posted callback.
  // TODO(yosrym93): Look into other options for handling this (also in getBody and getTrailers) as
  // they arise, e.g. cancellable posts, guaranteed ordering of posted callbacks and deletions, etc.
  CacheFilterWeakPtr self = weak_from_this();

  // The dispatcher needs to be captured because there's no guarantee that
  // decoder_callbacks_->dispatcher() is thread-safe.
  lookup_->getHeaders([self, &request_headers,
                       &dispatcher = decoder_callbacks_->dispatcher()](LookupResult&& result) {
    // The callback is posted to the dispatcher to make sure it is called on the worker thread.
    dispatcher.post(
        [self, &request_headers, status = result.cache_entry_status_,
         headers = std::move(result.headers_), range_details = std::move(result.range_details_),
         content_length = result.content_length_, has_trailers = result.has_trailers_]() mutable {
          if (CacheFilterSharedPtr cache_filter = self.lock()) {
            cache_filter->onHeaders(LookupResult{status, std::move(headers), content_length,
                                                 range_details, has_trailers},
                                    request_headers);
          }
        });
  });
}

void CacheFilter::getBody() {
  ASSERT(lookup_, "CacheFilter is trying to call getBody with no LookupContext");
  ASSERT(!remaining_ranges_.empty(), "No reason to call getBody when there's no body to get.");
  // If the cache posts a callback to the dispatcher then the CacheFilter is destroyed for any
  // reason (e.g client disconnected and HTTP stream terminated), then there is no guarantee that
  // the posted callback will run before the filter is deleted. Hence, a weak_ptr to the CacheFilter
  // is captured and used to make sure the CacheFilter is still alive before accessing it in the
  // posted callback.
  CacheFilterWeakPtr self = weak_from_this();

  // The dispatcher needs to be captured because there's no guarantee that
  // decoder_callbacks_->dispatcher() is thread-safe.
  lookup_->getBody(remaining_ranges_[0], [self, &dispatcher = decoder_callbacks_->dispatcher()](
                                             Buffer::InstancePtr&& body) {
    // The callback is posted to the dispatcher to make sure it is called on the worker thread.
    dispatcher.post([self, body = std::move(body)]() mutable {
      if (CacheFilterSharedPtr cache_filter = self.lock()) {
        cache_filter->onBody(std::move(body));
      }
    });
  });
}

void CacheFilter::getTrailers() {
  ASSERT(lookup_, "CacheFilter is trying to call getTrailers with no LookupContext");
  ASSERT(response_has_trailers_, "No reason to call getTrailers when there's no trailers to get.");

  // If the cache posts a callback to the dispatcher then the CacheFilter is destroyed for any
  // reason (e.g client disconnected and HTTP stream terminated), then there is no guarantee that
  // the posted callback will run before the filter is deleted. Hence, a weak_ptr to the CacheFilter
  // is captured and used to make sure the CacheFilter is still alive before accessing it in the
  // posted callback.
  CacheFilterWeakPtr self = weak_from_this();

  // The dispatcher needs to be captured because there's no guarantee that
  // decoder_callbacks_->dispatcher() is thread-safe.
  lookup_->getTrailers([self, &dispatcher = decoder_callbacks_->dispatcher()](
                           Http::ResponseTrailerMapPtr&& trailers) {
    // The callback is posted to the dispatcher to make sure it is called on the worker thread.
    // The lambda must be mutable as it captures trailers as a unique_ptr.
    dispatcher.post([self, trailers = std::move(trailers)]() mutable {
      if (CacheFilterSharedPtr cache_filter = self.lock()) {
        cache_filter->onTrailers(std::move(trailers));
      }
    });
  });
}

void CacheFilter::onHeaders(LookupResult&& result, Http::RequestHeaderMap& request_headers) {
  if (filter_state_ == FilterState::Destroyed) {
    // The filter is being destroyed, any callbacks should be ignored.
    return;
  }
  if (filter_state_ == FilterState::NotServingFromCache) {
    // A response was injected into the filter chain before the cache lookup finished, e.g. because
    // the request stream timed out.
    return;
  }

  // TODO(yosrym93): Handle request only-if-cached directive
  lookup_result_ = std::make_unique<LookupResult>(std::move(result));
  switch (lookup_result_->cache_entry_status_) {
  case CacheEntryStatus::FoundNotModified:
    PANIC("unsupported code");
  case CacheEntryStatus::RequiresValidation:
    // If a cache entry requires validation, inject validation headers in the
    // request and let it pass through as if no cache entry was found. If the
    // cache entry was valid, the response status should be 304 (unmodified)
    // and the cache entry will be injected in the response body.
    handleCacheHitWithValidation(request_headers);
    return;
  case CacheEntryStatus::Ok:
    if (lookup_result_->range_details_.has_value()) {
      handleCacheHitWithRangeRequest();
      return;
    }
    handleCacheHit();
    return;
  case CacheEntryStatus::Unusable:
    decoder_callbacks_->continueDecoding();
    return;
  case CacheEntryStatus::LookupError:
    filter_state_ = FilterState::NotServingFromCache;
    insert_status_ = InsertStatus::NoInsertLookupError;
    decoder_callbacks_->continueDecoding();
    return;
  }
  ENVOY_LOG(error, "Unhandled CacheEntryStatus in CacheFilter::onHeaders: {}",
            cacheEntryStatusString(lookup_result_->cache_entry_status_));
  // Treat unhandled status as a cache miss.
  decoder_callbacks_->continueDecoding();
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was being validated.
  if (filter_state_ == FilterState::Destroyed) {
    // The filter is being destroyed, any callbacks should be ignored.
    return;
  }
  ASSERT(!remaining_ranges_.empty(),
         "CacheFilter doesn't call getBody unless there's more body to get, so this is a "
         "bogus callback.");
  ASSERT(body, "Cache said it had a body, but isn't giving it to us.");

  const uint64_t bytes_from_cache = body->length();
  if (bytes_from_cache < remaining_ranges_[0].length()) {
    remaining_ranges_[0].trimFront(bytes_from_cache);
  } else if (bytes_from_cache == remaining_ranges_[0].length()) {
    remaining_ranges_.erase(remaining_ranges_.begin());
  } else {
    ASSERT(false, "Received oversized body from cache.");
    filter_state_ == FilterState::DecodeServingFromCache ? decoder_callbacks_->resetStream()
                                                         : encoder_callbacks_->resetStream();
    return;
  }

  const bool end_stream = remaining_ranges_.empty() && !response_has_trailers_;

  filter_state_ == FilterState::DecodeServingFromCache
      ? decoder_callbacks_->encodeData(*body, end_stream)
      : encoder_callbacks_->addEncodedData(*body, !response_has_trailers_);

  if (!remaining_ranges_.empty()) {
    getBody();
  } else if (response_has_trailers_) {
    getTrailers();
  } else {
    finalizeEncodingCachedResponse();
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was being validated.
  if (filter_state_ == FilterState::Destroyed) {
    // The filter is being destroyed, any callbacks should be ignored.
    return;
  }
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  } else {
    Http::ResponseTrailerMap& response_trailers = encoder_callbacks_->addEncodedTrailers();
    response_trailers = std::move(*trailers);
  }
  finalizeEncodingCachedResponse();
}

void CacheFilter::handleCacheHit() {
  filter_state_ = FilterState::DecodeServingFromCache;
  insert_status_ = InsertStatus::NoInsertCacheHit;
  encodeCachedResponse();
}

void CacheFilter::handleCacheHitWithRangeRequest() {
  if (!lookup_result_->range_details_.has_value()) {
    ENVOY_LOG(error, "handleCacheHitWithRangeRequest() should not be called without "
                     "range_details being populated in lookup_result_");
    return;
  }
  if (!lookup_result_->range_details_->satisfiable_) {
    filter_state_ = FilterState::DecodeServingFromCache;
    insert_status_ = InsertStatus::NoInsertCacheHit;
    lookup_result_->headers_->setStatus(
        static_cast<uint64_t>(Envoy::Http::Code::RangeNotSatisfiable));
    lookup_result_->headers_->addCopy(Envoy::Http::Headers::get().ContentRange,
                                      absl::StrCat("bytes */", lookup_result_->content_length_));
    // We shouldn't serve any of the body, so the response content length
    // is 0.
    lookup_result_->setContentLength(0);
    encodeCachedResponse();
    decoder_callbacks_->continueDecoding();
    return;
  }

  std::vector<AdjustedByteRange> ranges = lookup_result_->range_details_->ranges_;
  if (ranges.size() != 1) {
    // Multi-part responses are not supported, and they will be treated as
    // a usual 200 response. A possible way to achieve that would be to move
    // all ranges to remaining_ranges_, and add logic inside '::onBody' to
    // interleave the body bytes with sub-headers and separator string for
    // each part. Would need to keep track if the current range is over or
    // not to know when to insert the separator, and calculate the length
    // based on length of ranges + extra headers and separators.
    handleCacheHit();
    return;
  }

  filter_state_ = FilterState::DecodeServingFromCache;
  insert_status_ = InsertStatus::NoInsertCacheHit;

  lookup_result_->headers_->setStatus(static_cast<uint64_t>(Envoy::Http::Code::PartialContent));
  lookup_result_->headers_->addCopy(Envoy::Http::Headers::get().ContentRange,
                                    absl::StrCat("bytes ", ranges[0].begin(), "-",
                                                 ranges[0].end() - 1, "/",
                                                 lookup_result_->content_length_));
  // We serve only the desired range, so adjust the length
  // accordingly.
  lookup_result_->setContentLength(ranges[0].length());
  remaining_ranges_ = std::move(ranges);
  encodeCachedResponse();
  decoder_callbacks_->continueDecoding();
}

void CacheFilter::handleCacheHitWithValidation(Envoy::Http::RequestHeaderMap& request_headers) {
  filter_state_ = FilterState::ValidatingCachedResponse;
  injectValidationHeaders(request_headers);
  decoder_callbacks_->continueDecoding();
}

void CacheFilter::processSuccessfulValidation(Http::ResponseHeaderMap& response_headers) {
  ASSERT(lookup_result_, "CacheFilter trying to validate a non-existent lookup result");
  ASSERT(
      filter_state_ == FilterState::ValidatingCachedResponse,
      "processSuccessfulValidation must only be called when a cached response is being validated");
  ASSERT(isResponseNotModified(response_headers),
         "processSuccessfulValidation must only be called with 304 responses");

  // Check whether the cached entry should be updated before modifying the 304 response.
  const bool should_update_cached_entry = shouldUpdateCachedEntry(response_headers);

  filter_state_ = FilterState::EncodeServingFromCache;

  // Update the 304 response status code and content-length
  response_headers.setStatus(lookup_result_->headers_->getStatusValue());
  response_headers.setContentLength(lookup_result_->headers_->getContentLengthValue());

  // A response that has been validated should not contain an Age header as it is equivalent to a
  // freshly served response from the origin, unless the 304 response has an Age header, which
  // means it was served by an upstream cache.
  // Remove any existing Age header in the cached response.
  lookup_result_->headers_->removeInline(CacheCustomHeaders::age());

  // Add any missing headers from the cached response to the 304 response.
  lookup_result_->headers_->iterate([&response_headers](const Http::HeaderEntry& cached_header) {
    // TODO(yosrym93): Try to avoid copying the header key twice.
    Http::LowerCaseString key(std::string(cached_header.key().getStringView()));
    absl::string_view value = cached_header.value().getStringView();
    if (response_headers.get(key).empty()) {
      response_headers.setCopy(key, value);
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  if (should_update_cached_entry) {
    // TODO(yosrym93): else the cached entry should be deleted.
    // Update metadata associated with the cached response. Right now this is only response_time;
    const ResponseMetadata metadata = {time_source_.systemTime()};
    cache_->updateHeaders(*lookup_, response_headers, metadata,
                          [](bool updated ABSL_ATTRIBUTE_UNUSED) {});
    insert_status_ = InsertStatus::HeaderUpdate;
  }

  // A cache entry was successfully validated -> encode cached body and trailers.
  encodeCachedResponse();
}

// TODO(yosrym93): Write a test that exercises this when SimpleHttpCache implements updateHeaders
bool CacheFilter::shouldUpdateCachedEntry(const Http::ResponseHeaderMap& response_headers) const {
  ASSERT(isResponseNotModified(response_headers),
         "shouldUpdateCachedEntry must only be called with 304 responses");
  ASSERT(lookup_result_, "shouldUpdateCachedEntry precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(filter_state_ == FilterState::ValidatingCachedResponse,
         "shouldUpdateCachedEntry precondition unsatisfied: the "
         "CacheFilter is not validating a cache lookup result");

  // According to: https://httpwg.org/specs/rfc7234.html#freshening.responses,
  // and assuming a single cached response per key:
  // If the 304 response contains a strong validator (etag) that does not match the cached response,
  // the cached response should not be updated.
  const Http::HeaderEntry* response_etag = response_headers.getInline(CacheCustomHeaders::etag());
  const Http::HeaderEntry* cached_etag =
      lookup_result_->headers_->getInline(CacheCustomHeaders::etag());
  return !response_etag || (cached_etag && cached_etag->value().getStringView() ==
                                               response_etag->value().getStringView());
}

void CacheFilter::injectValidationHeaders(Http::RequestHeaderMap& request_headers) {
  ASSERT(lookup_result_, "injectValidationHeaders precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(filter_state_ == FilterState::ValidatingCachedResponse,
         "injectValidationHeaders precondition unsatisfied: the "
         "CacheFilter is not validating a cache lookup result");

  const Http::HeaderEntry* etag_header =
      lookup_result_->headers_->getInline(CacheCustomHeaders::etag());
  const Http::HeaderEntry* last_modified_header =
      lookup_result_->headers_->getInline(CacheCustomHeaders::lastModified());

  if (etag_header) {
    absl::string_view etag = etag_header->value().getStringView();
    request_headers.setInline(CacheCustomHeaders::ifNoneMatch(), etag);
  }
  if (DateUtil::timePointValid(CacheHeadersUtils::httpTime(last_modified_header))) {
    // Valid Last-Modified header exists.
    absl::string_view last_modified = last_modified_header->value().getStringView();
    request_headers.setInline(CacheCustomHeaders::ifModifiedSince(), last_modified);
  } else {
    // Either Last-Modified is missing or invalid, fallback to Date.
    // A correct behaviour according to:
    // https://httpwg.org/specs/rfc7232.html#header.if-modified-since
    absl::string_view date = lookup_result_->headers_->getDateValue();
    request_headers.setInline(CacheCustomHeaders::ifModifiedSince(), date);
  }
}

void CacheFilter::encodeCachedResponse() {
  ASSERT(lookup_result_, "encodeCachedResponse precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");

  response_has_trailers_ = lookup_result_->has_trailers_;
  const bool end_stream =
      (lookup_result_->content_length_ == 0 && !response_has_trailers_) || is_head_request_;

  // Set appropriate response flags and codes.
  Http::StreamFilterCallbacks* callbacks =
      filter_state_ == FilterState::DecodeServingFromCache
          ? static_cast<Http::StreamFilterCallbacks*>(decoder_callbacks_)
          : static_cast<Http::StreamFilterCallbacks*>(encoder_callbacks_);

  callbacks->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  callbacks->streamInfo().setResponseCodeDetails(
      CacheResponseCodeDetails::get().ResponseFromCacheFilter);

  // If the filter is encoding, 304 response headers and cached headers are merged in encodeHeaders.
  // If the filter is decoding, we need to serve response headers from cache directly.
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    decoder_callbacks_->encodeHeaders(std::move(lookup_result_->headers_), end_stream,
                                      CacheResponseCodeDetails::get().ResponseFromCacheFilter);
  }
  if (filter_state_ == FilterState::EncodeServingFromCache && is_head_request_) {
    filter_state_ = FilterState::ResponseServedFromCache;
    return;
  }
  if (lookup_result_->content_length_ > 0 && !is_head_request_) {
    // No range has been added, so we add entire body to the response.
    if (remaining_ranges_.empty()) {
      remaining_ranges_.emplace_back(0, lookup_result_->content_length_);
    }
    getBody();
  } else if (response_has_trailers_) {
    getTrailers();
  }
}

void CacheFilter::finalizeEncodingCachedResponse() {
  if (filter_state_ == FilterState::EncodeServingFromCache) {
    // encodeHeaders returned StopIteration waiting for finishing encoding the cached response --
    // continue encoding.
    encoder_callbacks_->continueEncoding();
  }
  filter_state_ = FilterState::ResponseServedFromCache;
}

LookupStatus CacheFilter::lookupStatus() const {
  if (lookup_result_ == nullptr && lookup_ != nullptr) {
    return LookupStatus::RequestIncomplete;
  }

  if (lookup_result_ != nullptr) {
    return resolveLookupStatus(lookup_result_->cache_entry_status_, filter_state_);
  } else {
    return resolveLookupStatus(absl::nullopt, filter_state_);
  }
}

InsertStatus CacheFilter::insertStatus() const {
  return insert_status_.value_or((insert_queue_ == nullptr)
                                     ? InsertStatus::NoInsertRequestIncomplete
                                     : InsertStatus::InsertAbortedResponseIncomplete);
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
