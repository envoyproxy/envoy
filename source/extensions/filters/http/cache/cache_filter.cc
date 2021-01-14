#include "extensions/filters/http/cache/cache_filter.h"

#include "envoy/http/header_map.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/cache/cacheability_utils.h"
#include "extensions/filters/http/cache/inline_headers_handles.h"

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

CacheFilter::CacheFilter(
    const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config, const std::string&,
    Stats::Scope&, TimeSource& time_source, HttpCache& http_cache)
    : time_source_(time_source), cache_(http_cache),
      vary_allow_list_(config.allowed_vary_headers()) {}

void CacheFilter::onDestroy() {
  filter_state_ = FilterState::Destroyed;
  if (lookup_) {
    lookup_->onDestroy();
  }
  if (insert_) {
    insert_->onDestroy();
  }
}

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                     bool end_stream) {
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders: {}", *decoder_callbacks_, headers);
  if (!end_stream) {
    ENVOY_STREAM_LOG(
        debug,
        "CacheFilter::decodeHeaders ignoring request because it has body and/or trailers: {}",
        *decoder_callbacks_, headers);
    return Http::FilterHeadersStatus::Continue;
  }
  if (!CacheabilityUtils::isCacheableRequest(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders ignoring uncacheable request: {}",
                     *decoder_callbacks_, headers);
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);

  LookupRequest lookup_request(headers, time_source_.systemTime(), vary_allow_list_);
  request_allows_inserts_ = !lookup_request.requestCacheControl().no_store_;
  lookup_ = cache_.makeLookupContext(std::move(lookup_request));

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

  if (filter_state_ == FilterState::ValidatingCachedResponse && isResponseNotModified(headers)) {
    processSuccessfulValidation(headers);
    // Stop the encoding stream until the cached response is fetched & added to the encoding stream.
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Either a cache miss or a cache entry that is no longer valid.
  // Check if the new response can be cached.
  if (request_allows_inserts_ &&
      CacheabilityUtils::isCacheableResponse(headers, vary_allow_list_)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting headers", *encoder_callbacks_);
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    // Add metadata associated with the cached response. Right now this is only response_time;
    const ResponseMetadata metadata = {time_source_.systemTime()};
    insert_->insertHeaders(headers, metadata, end_stream);
  }
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
  if (insert_) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeData inserting body", *encoder_callbacks_);
    // TODO(toddmgreer): Wait for the cache if necessary.
    insert_->insertBody(
        data, [](bool) {}, end_stream);
  }
  return Http::FilterDataStatus::Continue;
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
    // The lambda passed to dispatcher.post() needs to be copyable as it will be used to
    // initialize a std::function. Therefore, it cannot capture anything non-copyable.
    // LookupResult is non-copyable as LookupResult::headers_ is a unique_ptr, which is
    // non-copyable. Hence, "result" is decomposed when captured, and re-instantiated inside the
    // lambda so that "result.headers_" can be captured as a raw pointer, then wrapped in a
    // unique_ptr when the result is re-instantiated.
    dispatcher.post([self, &request_headers, status = result.cache_entry_status_,
                     headers_raw_ptr = result.headers_.release(),
                     response_ranges = std::move(result.response_ranges_),
                     content_length = result.content_length_,
                     has_trailers = result.has_trailers_]() mutable {
      // Wrap the raw pointer in a unique_ptr before checking to avoid memory leaks.
      Http::ResponseHeaderMapPtr headers = absl::WrapUnique(headers_raw_ptr);
      if (CacheFilterSharedPtr cache_filter = self.lock()) {
        cache_filter->onHeaders(
            LookupResult{status, std::move(headers), content_length, response_ranges, has_trailers},
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
    // The lambda passed to dispatcher.post() needs to be copyable as it will be used to
    // initialize a std::function. Therefore, it cannot capture anything non-copyable.
    // "body" is a unique_ptr, which is non-copyable. Hence, it is captured as a raw pointer then
    // wrapped in a unique_ptr inside the lambda.
    dispatcher.post([self, body_raw_ptr = body.release()] {
      // Wrap the raw pointer in a unique_ptr before checking to avoid memory leaks.
      Buffer::InstancePtr body = absl::WrapUnique(body_raw_ptr);
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
    // The lambda passed to dispatcher.post() needs to be copyable as it will be used to
    // initialize a std::function. Therefore, it cannot capture anything non-copyable.
    // "trailers" is a unique_ptr, which is non-copyable. Hence, it is captured as a raw
    // pointer then wrapped in a unique_ptr inside the lambda.
    dispatcher.post([self, trailers_raw_ptr = trailers.release()] {
      // Wrap the raw pointer in a unique_ptr before checking to avoid memory leaks.
      Http::ResponseTrailerMapPtr trailers = absl::WrapUnique(trailers_raw_ptr);
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
  // TODO(yosrym93): Handle request only-if-cached directive
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::FoundNotModified:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // We don't yet return or support these codes.
  case CacheEntryStatus::RequiresValidation:
    // If a cache entry requires validation, inject validation headers in the request and let it
    // pass through as if no cache entry was found.
    // If the cache entry was valid, the response status should be 304 (unmodified) and the cache
    // entry will be injected in the response body.
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    filter_state_ = FilterState::ValidatingCachedResponse;
    injectValidationHeaders(request_headers);
    break;
  case CacheEntryStatus::Unusable:
    break;
  case CacheEntryStatus::NotSatisfiableRange:
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    filter_state_ = FilterState::DecodeServingFromCache;
    lookup_result_->headers_->setStatus(static_cast<uint64_t>(Http::Code::RangeNotSatisfiable));
    lookup_result_->headers_->addCopy(Http::Headers::get().ContentRange,
                                      absl::StrCat("bytes */", lookup_result_->content_length_));
    // We shouldn't serve any of the body, so the response content length is 0.
    lookup_result_->setContentLength(0);
    encodeCachedResponse();
    break;
  case CacheEntryStatus::SatisfiableRange:
    if (result.response_ranges_.size() == 1) {
      lookup_result_ = std::make_unique<LookupResult>(std::move(result));
      filter_state_ = FilterState::DecodeServingFromCache;
      lookup_result_->headers_->setStatus(static_cast<uint64_t>(Http::Code::PartialContent));
      lookup_result_->headers_->addCopy(
          Http::Headers::get().ContentRange,
          absl::StrCat("bytes ", lookup_result_->response_ranges_[0].begin(), "-",
                       lookup_result_->response_ranges_[0].end() - 1, "/",
                       lookup_result_->content_length_));
      // We serve only the desired range, so adjust the length accordingly.
      lookup_result_->setContentLength(lookup_result_->response_ranges_[0].length());
      remaining_ranges_ = std::move(lookup_result_->response_ranges_);
      encodeCachedResponse();
      break;
    }
    // Multi-part responses are not supported, and they will be treated as a usual 200 response on
    // ::Ok case below. A possible way to achieve that would be to move all ranges to
    // remaining_ranges_, and add logic inside '::onBody' to interleave the body bytes with
    // sub-headers and separator string for each part. Would need to keep track if the current range
    // is over or not to know when to insert the separator, and calculate the length based on length
    // of ranges + extra headers and separators.
    ABSL_FALLTHROUGH_INTENDED;
  case CacheEntryStatus::Ok:
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    filter_state_ = FilterState::DecodeServingFromCache;
    encodeCachedResponse();
    // Return here so that continueDecoding is not called.
    // No need to continue the decoding stream as a cached response is already being served.
    return;
  }
  // decodeHeaders returned StopIteration waiting for this callback -- continue decoding
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
      : encoder_callbacks_->addEncodedData(*body, true);

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
  lookup_result_->headers_->removeInline(age_handle.handle());

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
    cache_.updateHeaders(*lookup_, response_headers, metadata);
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
  const Http::HeaderEntry* response_etag = response_headers.getInline(etag_handle.handle());
  const Http::HeaderEntry* cached_etag = lookup_result_->headers_->getInline(etag_handle.handle());
  return !response_etag || (cached_etag && cached_etag->value().getStringView() ==
                                               response_etag->value().getStringView());
}

void CacheFilter::injectValidationHeaders(Http::RequestHeaderMap& request_headers) {
  ASSERT(lookup_result_, "injectValidationHeaders precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(filter_state_ == FilterState::ValidatingCachedResponse,
         "injectValidationHeaders precondition unsatisfied: the "
         "CacheFilter is not validating a cache lookup result");

  const Http::HeaderEntry* etag_header = lookup_result_->headers_->getInline(etag_handle.handle());
  const Http::HeaderEntry* last_modified_header =
      lookup_result_->headers_->getInline(last_modified_handle.handle());

  if (etag_header) {
    absl::string_view etag = etag_header->value().getStringView();
    request_headers.setInline(if_none_match_handle.handle(), etag);
  }
  if (DateUtil::timePointValid(CacheHeadersUtils::httpTime(last_modified_header))) {
    // Valid Last-Modified header exists.
    absl::string_view last_modified = last_modified_header->value().getStringView();
    request_headers.setInline(if_modified_since_handle.handle(), last_modified);
  } else {
    // Either Last-Modified is missing or invalid, fallback to Date.
    // A correct behaviour according to:
    // https://httpwg.org/specs/rfc7232.html#header.if-modified-since
    absl::string_view date = lookup_result_->headers_->getDateValue();
    request_headers.setInline(if_modified_since_handle.handle(), date);
  }
}

void CacheFilter::encodeCachedResponse() {
  ASSERT(lookup_result_, "encodeCachedResponse precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");

  response_has_trailers_ = lookup_result_->has_trailers_;
  const bool end_stream = (lookup_result_->content_length_ == 0 && !response_has_trailers_);

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

  if (lookup_result_->content_length_ > 0) {
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

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
