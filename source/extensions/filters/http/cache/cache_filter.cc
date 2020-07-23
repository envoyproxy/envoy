#include "extensions/filters/http/cache/cache_filter.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "extensions/filters/http/cache/cacheability_utils.h"

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
  filter_state_ = FilterState::Decoding;
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

  // Used exclusively to access the RequestHeaderMap in onHeaders
  request_headers_ = &headers;
  getHeaders();

  if (get_headers_state_ == GetHeadersState::GetHeadersResultUnusable) {
    // onHeaders has already been called, and no usable cache entry was found--continue iteration.
    return Http::FilterHeadersStatus::Continue;
  }
  // onHeaders hasn't been called yet--stop iteration to wait for it, and tell it that we stopped
  // iteration.
  get_headers_state_ = GetHeadersState::FinishedGetHeadersCall;
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) {
  if (skip_encoding_) {
    return Http::FilterHeadersStatus::Continue;
  }

  filter_state_ = FilterState::Encoding;

  // If lookup_ is null, the request wasn't cacheable, so the response isn't either.
  if (!lookup_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (validating_cache_entry_ && isResponseNotModified(headers)) {
    return processSuccessfulValidation(headers);
  }

  // Either a cache miss or a cache entry is no longer valid
  // Check if the new response can be cached
  if (request_allows_inserts_ && CacheabilityUtils::isCacheableResponse(headers)) {
    // TODO(#12140): Add date internal header or metadata to cached responses
    ASSERT(lookup_, "CacheFilter::encodeHeaders no LookupContext to use to insert data");
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting headers", *encoder_callbacks_);
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    insert_->insertHeaders(headers, end_stream);
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (skip_encoding_) {
    return Http::FilterDataStatus::Continue;
  }
  if (insert_) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeData inserting body", *encoder_callbacks_);
    // TODO(toddmgreer): Wait for the cache if necessary.
    insert_->insertBody(
        data, [](bool) {}, end_stream);
    return Http::FilterDataStatus::Continue;
  }
  if (encode_cached_response_state_ == EncodeCachedResponseState::IterationStopped) {
    // Iteration stopped waiting for cached body (and trailers) to be encoded
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

void CacheFilter::getHeaders() {
  ASSERT(lookup_, "CacheFilter is trying to call getHeaders with no LookupContext");
  lookup_->getHeaders([this](LookupResult&& result) { onHeaders(std::move(result)); });
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

void CacheFilter::onHeaders(LookupResult&& result) {
  // TODO(yosrym93): Handle request only-if-cached directive
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::UnsatisfiableRange:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // We don't yet return or support these codes.
  case CacheEntryStatus::RequiresValidation:
    // If a cache entry requires validation inject validation headers in the request and let it
    // pass through as if no cache entry was found. If the cache entry was valid the response
    // status should be 304 (unmodified), and the cache entry will be injected in the response body
    validating_cache_entry_ = true;
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    injectValidationHeaders();
    FALLTHRU;
  case CacheEntryStatus::Unusable:
    if (get_headers_state_ == GetHeadersState::FinishedGetHeadersCall) {
      // decodeHeader returned Http::FilterHeadersStatus::StopAllIterationAndWatermark--restart it
      decoder_callbacks_->continueDecoding();
    } else {
      // decodeHeader hasn't yet returned--tell it to return Http::FilterHeadersStatus::Continue.
      get_headers_state_ = GetHeadersState::GetHeadersResultUnusable;
    }
    break;
  case CacheEntryStatus::Ok:
    lookup_result_ = std::make_unique<LookupResult>(std::move(result));
    encodeCachedResponse();
  }
  // All switch case paths should lead to this line
  // Stored pointer to RequestHeaderMap is no longer needed
  request_headers_ = nullptr;
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
  // Can be called during decoding if a valid cache hit is found
  // or during encoding if a cache entry was being validated
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
    filter_state_ == FilterState::Encoding ? encoder_callbacks_->resetStream()
                                           : decoder_callbacks_->resetStream();
    return;
  }

  const bool end_stream = remaining_body_.empty() && !response_has_trailers_;

  filter_state_ == FilterState::Encoding ? encoder_callbacks_->addEncodedData(*body, true)
                                         : decoder_callbacks_->encodeData(*body, end_stream);

  if (!remaining_body_.empty()) {
    getBody();
  } else if (response_has_trailers_) {
    getTrailers();
  } else {
    finishedEncodingCachedResponse();
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  // Can be called during decoding if a valid cache hit is found
  // or during encoding if a cache entry was being validated
  if (filter_state_ == FilterState::Encoding) {
    Http::ResponseTrailerMap& response_trailers = encoder_callbacks_->addEncodedTrailers();
    response_trailers = std::move(*trailers);
  } else {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
  finishedEncodingCachedResponse();
}

Http::FilterHeadersStatus
CacheFilter::processSuccessfulValidation(Http::ResponseHeaderMap& response_headers) {
  ASSERT(lookup_result_, "CacheFilter trying to validate a non-existent lookup result");
  ASSERT(
      validating_cache_entry_,
      "processSuccessfulValidation must only be called when a cached response is being validated");
  ASSERT(isResponseNotModified(response_headers),
         "processSuccessfulValidation must only be called with 304 responses");

  // Check whether the cached entry should be updated before modifying the 304 response
  const bool should_update_cached_entry = shouldUpdateCachedEntry(response_headers);

  // Update the 304 response status code and content-length
  response_headers.setStatus(lookup_result_->headers_->getStatusValue());
  response_headers.setContentLength(lookup_result_->headers_->getContentLengthValue());

  // A cache entry was successfully validated; encode cached body and trailers.
  // encodeCachedResponse also adds the age header to lookup_result_
  // so it should be called before headers are merged.
  encodeCachedResponse();

  // Add any missing headers from the cached response to the 304 response.
  lookup_result_->headers_->iterate([&response_headers](const Http::HeaderEntry& cached_header) {
    // TODO(yosrym93): Try to avoid copying the header key twice
    Http::LowerCaseString key(std::string(cached_header.key().getStringView()));
    absl::string_view value = cached_header.value().getStringView();
    if (!response_headers.get(key)) {
      response_headers.setCopy(key, value);
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  if (should_update_cached_entry) {
    // TODO(yosrym93): else the cached entry should be deleted
    cache_.updateHeaders(*lookup_, response_headers);
  }

  if (encode_cached_response_state_ == EncodeCachedResponseState::FinishedEncoding) {
    // Cached body (and trailers) already encoded -- continue iteration
    return Http::FilterHeadersStatus::Continue;
  } else {
    // Encoding body (and trailers) not finished yet, stop iteration to wait for it
    // and tell it that iteration stopped
    encode_cached_response_state_ = EncodeCachedResponseState::IterationStopped;
    return Http::FilterHeadersStatus::StopIteration;
  }
}

bool CacheFilter::shouldUpdateCachedEntry(const Http::ResponseHeaderMap& response_headers) const {
  ASSERT(isResponseNotModified(response_headers),
         "shouldUpdateCachedEntry must only be called with 304 responses");
  ASSERT(lookup_result_, "shouldUpdateCachedEntry precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(validating_cache_entry_, "shouldUpdateCachedEntry precondition unsatisfied: the "
                                  "CacheFilter is not validating a cache lookup result");

  // According to: https://httpwg.org/specs/rfc7234.html#freshening.responses,
  // and assuming a single cached response per key:
  // If the 304 response contains a strong validator (etag) that does not match the cached response
  // the cached response should not be updated
  const auto response_etag_ptr = response_headers.get(Http::CustomHeaders::get().Etag);
  const auto cached_etag_ptr = lookup_result_->headers_->get(Http::CustomHeaders::get().Etag);
  return !response_etag_ptr || (cached_etag_ptr && cached_etag_ptr->value().getStringView() ==
                                                       response_etag_ptr->value().getStringView());
}

void CacheFilter::injectValidationHeaders() {
  ASSERT(lookup_result_, "injectValidationHeaders precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");
  ASSERT(validating_cache_entry_, "injectValidationHeaders precondition unsatisfied: the "
                                  "CacheFilter is not validating a cache lookup result");
  ASSERT(request_headers_, "injectValidationHeaders precondition unsatisfied: request_headers_ "
                           "does not point to the RequestHeadersMap of the current request");
  const Http::HeaderEntry* etag_header =
      lookup_result_->headers_->get(Http::CustomHeaders::get().Etag);
  const Http::HeaderEntry* last_modified_header =
      lookup_result_->headers_->get(Http::CustomHeaders::get().LastModified);

  if (etag_header) {
    absl::string_view etag = etag_header->value().getStringView();
    request_headers_->setReferenceKey(Http::CustomHeaders::get().IfNonMatch, etag);
  }
  if (CacheHeadersUtils::httpTime(last_modified_header) != SystemTime()) {
    // Valid Last-Modified header exists
    std::cout << "LAST MODIFIED" << std::endl;
    absl::string_view last_modified = last_modified_header->value().getStringView();
    request_headers_->setCopy(Http::CustomHeaders::get().IfModifiedSince, last_modified);
  } else {
    // Either Last-Modified is missing or invalid, fallback to Date
    std::cout << "DATE FALBACK" << std::endl;
    absl::string_view date = lookup_result_->headers_->getDateValue();
    request_headers_->setCopy(Http::CustomHeaders::get().IfModifiedSince, date);
  }
}

void CacheFilter::encodeCachedResponse() {
  ASSERT(lookup_result_, "encodeCachedResponse precondition unsatisfied: lookup_result_ "
                         "does not point to a cache lookup result");

  response_has_trailers_ = lookup_result_->has_trailers_;
  const bool end_stream = (lookup_result_->content_length_ == 0 && !response_has_trailers_);
  // TODO(toddmgreer): Calculate age per https://httpwg.org/specs/rfc7234.html#age.calculations
  lookup_result_->headers_->addReferenceKey(Http::Headers::get().Age, 0);

  // Set appropriate response flags and codes
  Http::StreamFilterCallbacks* callbacks_ =
      filter_state_ == FilterState::Encoding
          ? static_cast<Http::StreamFilterCallbacks*>(encoder_callbacks_)
          : static_cast<Http::StreamFilterCallbacks*>(decoder_callbacks_);

  callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  callbacks_->streamInfo().setResponseCodeDetails(
      CacheResponseCodeDetails::get().ResponseFromCacheFilter);

  // If the filter is encoding, 304 response headers and cached headers are merged in encodeHeaders
  // If the filter is decoding, we need to serve response headers from cache directly
  if (filter_state_ == FilterState::Decoding) {
    // decoder_callbacks_->encodeHeaders() will invoke CacheFilter::encodeHeaders, skip it
    skip_encoding_ = true;
    decoder_callbacks_->encodeHeaders(std::move(lookup_result_->headers_), end_stream);
  }

  if (lookup_result_->content_length_ > 0) {
    remaining_body_.emplace_back(0, lookup_result_->content_length_);
    getBody();
  } else if (response_has_trailers_) {
    getTrailers();
  }
}

void CacheFilter::finishedEncodingCachedResponse() {
  if (filter_state_ == FilterState::Decoding) {
    // Nothing needs to be done if a cache hit was found while decoding a request
    return;
  }
  if (encode_cached_response_state_ == EncodeCachedResponseState::IterationStopped) {
    // Filter iteration was stopped waiting for encoding cached response to finish -- continue
    encoder_callbacks_->continueEncoding();
  }
  encode_cached_response_state_ = EncodeCachedResponseState::FinishedEncoding;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
