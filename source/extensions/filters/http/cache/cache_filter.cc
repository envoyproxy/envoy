#include "extensions/filters/http/cache/cache_filter.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/cache/cacheability_utils.h"
#include "extensions/filters/http/cache/inline_headers_handles.h"

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

CacheFilter::CacheFilter(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig&,
                         const std::string&, Stats::Scope&, TimeSource& time_source,
                         HttpCache& http_cache)
    : time_source_(time_source), cache_(http_cache) {}

void CacheFilter::onDestroy() {
  lookup_ = nullptr;
  insert_ = nullptr;
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

  LookupRequest lookup_request(headers, time_source_.systemTime());
  request_allows_inserts_ = !lookup_request.requestCacheControl().no_store_;
  lookup_ = cache_.makeLookupContext(std::move(lookup_request));

  ASSERT(lookup_);
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders starting lookup", *decoder_callbacks_);

  lookup_->getHeaders(
      [this, &headers](LookupResult&& result) { onHeaders(std::move(result), headers); });

  // If the cache called onHeaders synchronously it will have advanced the filter_state_.
  switch (filter_state_) {
  case FilterState::Initial:
    // Headers are not fetched from cache yet -- wait until cache lookup is completed.
    filter_state_ = FilterState::WaitingForCacheLookup;
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  case FilterState::DecodeServingFromCache:
  case FilterState::ResponseServedFromCache:
    // A fresh cached response was found -- no need to continue the decoding stream.
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  default:
    return Http::FilterHeadersStatus::Continue;
  }
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) {
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    // This call was invoked by decoder_callbacks_->encodeHeaders -- ignore it.
    return Http::FilterHeadersStatus::Continue;
  }

  // If lookup_ is null, the request wasn't cacheable, so the response isn't either.
  if (!lookup_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (filter_state_ == FilterState::ValidatingCachedResponse && isResponseNotModified(headers)) {
    processSuccessfulValidation(headers);
    if (filter_state_ != FilterState::ResponseServedFromCache) {
      // Response is still being fetched from cache -- wait until it is fetched & encoded.
      filter_state_ = FilterState::WaitingForCacheBody;
      return Http::FilterHeadersStatus::StopIteration;
    }
    return Http::FilterHeadersStatus::Continue;
  }

  // Either a cache miss or a cache entry that is no longer valid.
  // Check if the new response can be cached.
  if (request_allows_inserts_ && CacheabilityUtils::isCacheableResponse(headers)) {
    // TODO(#12140): Add date internal header or metadata to cached responses.
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting headers", *encoder_callbacks_);
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    insert_->insertHeaders(headers, end_stream);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (filter_state_ == FilterState::DecodeServingFromCache) {
    // This call was invoked by decoder_callbacks_->encodeData -- ignore it.
    return Http::FilterDataStatus::Continue;
  }
  if (filter_state_ == FilterState::WaitingForCacheBody) {
    // Encoding stream stopped waiting for cached body (and trailers) to be encoded.
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

void CacheFilter::getBody() {
  ASSERT(lookup_, "CacheFilter is trying to call getBody with no LookupContext");
  ASSERT(!remaining_body_.empty(), "No reason to call getBody when there's no body to get.");
  lookup_->getBody(remaining_body_[0],
                   [this](Buffer::InstancePtr&& body) { onBody(std::move(body)); });
}

void CacheFilter::getTrailers() {
  ASSERT(lookup_, "CacheFilter is trying to call getTrailers with no LookupContext");
  ASSERT(response_has_trailers_, "No reason to call getTrailers when there's no trailers to get.");
  lookup_->getTrailers(
      [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
}

void CacheFilter::onHeaders(LookupResult&& result, Http::RequestHeaderMap& request_headers) {
  // TODO(yosrym93): Handle request only-if-cached directive.
  bool should_continue_decoding = false;
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::NotSatisfiableRange: // TODO(#10132): create 416 response.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;          // We don't yet return or support these codes.
  case CacheEntryStatus::RequiresValidation:
    // If a cache entry requires validation, inject validation headers in the request and let it
    // pass through as if no cache entry was found.
    // If the cache entry was valid, the response status should be 304 (unmodified) and the cache
    // entry will be injected in the response body.
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    should_continue_decoding = filter_state_ == FilterState::WaitingForCacheLookup;
    filter_state_ = FilterState::ValidatingCachedResponse;
    injectValidationHeaders(request_headers);
    break;
  case CacheEntryStatus::Unusable:
    should_continue_decoding = filter_state_ == FilterState::WaitingForCacheLookup;
    filter_state_ = FilterState::NoCachedResponseFound;
    break;
  case CacheEntryStatus::SatisfiableRange: // TODO(#10132): break response content to the ranges
                                           // requested.
  case CacheEntryStatus::Ok:
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    filter_state_ = FilterState::DecodeServingFromCache;
    encodeCachedResponse();
  }
  if (should_continue_decoding) {
    // decodeHeaders returned StopIteration waiting for this callback -- continue decoding.
    decoder_callbacks_->continueDecoding();
  }
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
  // Can be called during decoding if a valid cache hit is found,
  // or during encoding if a cache entry was being validated.
  ASSERT(!remaining_body_.empty(),
         "CacheFilter doesn't call getBody unless there's more body to get, so this is a "
         "bogus callback.");
  ASSERT(body, "Cache said it had a body, but isn't giving it to us.");

  const uint64_t bytes_from_cache = body->length();
  if (bytes_from_cache < remaining_body_[0].length()) {
    remaining_body_[0].trimFront(bytes_from_cache);
  } else if (bytes_from_cache == remaining_body_[0].length()) {
    remaining_body_.erase(remaining_body_.begin());
  } else {
    ASSERT(false, "Received oversized body from cache.");
    filter_state_ == FilterState::DecodeServingFromCache ? decoder_callbacks_->resetStream()
                                                         : encoder_callbacks_->resetStream();
    return;
  }

  const bool end_stream = remaining_body_.empty() && !response_has_trailers_;

  filter_state_ == FilterState::DecodeServingFromCache
      ? decoder_callbacks_->encodeData(*body, end_stream)
      : encoder_callbacks_->addEncodedData(*body, true);

  if (!remaining_body_.empty()) {
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

  // Update the 304 response status code and content-length.
  response_headers.setStatus(lookup_result_->headers_->getStatusValue());
  response_headers.setContentLength(lookup_result_->headers_->getContentLengthValue());

  // A cache entry was successfully validated -> encode cached body and trailers.
  // encodeCachedResponse also adds the age header to lookup_result_
  // so it should be called before headers are merged.
  encodeCachedResponse();

  // Add any missing headers from the cached response to the 304 response.
  lookup_result_->headers_->iterate([&response_headers](const Http::HeaderEntry& cached_header) {
    // TODO(yosrym93): Try to avoid copying the header key twice.
    Http::LowerCaseString key(std::string(cached_header.key().getStringView()));
    absl::string_view value = cached_header.value().getStringView();
    if (!response_headers.get(key)) {
      response_headers.setCopy(key, value);
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  if (should_update_cached_entry) {
    // TODO(yosrym93): else the cached entry should be deleted.
    cache_.updateHeaders(*lookup_, response_headers);
  }
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
  if (CacheHeadersUtils::httpTime(last_modified_header) != SystemTime()) {
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
  // TODO(toddmgreer): Calculate age per https://httpwg.org/specs/rfc7234.html#age.calculations
  lookup_result_->headers_->addReferenceKey(Http::Headers::get().Age, 0);

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
    decoder_callbacks_->encodeHeaders(std::move(lookup_result_->headers_), end_stream);
  }

  if (lookup_result_->content_length_ > 0) {
    remaining_body_.emplace_back(0, lookup_result_->content_length_);
    getBody();
  } else if (response_has_trailers_) {
    getTrailers();
  }
}

void CacheFilter::finalizeEncodingCachedResponse() {
  if (filter_state_ == FilterState::WaitingForCacheBody) {
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
