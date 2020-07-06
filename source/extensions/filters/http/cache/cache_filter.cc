#include "extensions/filters/http/cache/cache_filter.h"

#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "common/http/headers.h"

#include "extensions/filters/http/cache/cacheability_utils.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

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
  lookup_->getHeaders([this](LookupResult&& result) { onHeaders(std::move(result)); });
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
  // Encoding was initiated by the CacheFilter itself because a cache hit was found during decoding
  if (decoding_cache_hit_) {
    return Http::FilterHeadersStatus::Continue;
  }

  filter_state_ = FilterState::Encoding;
  // If lookup_ is null, the request wasn't cacheable, so the response isn't either
  if (!lookup_) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (cache_entry_validation_ && result_ && headers.getStatusValue() == "304") {
    // A cache entry was successfuly validated
    // The origin server might not send all response headers in a 304 response
    // Therefore, any cached headers missing from the 304 response are added
    // The cached headers should not be served as is as the 304 response
    // may contain updated headers
    result_->headers_->iterate(
        [response_headers = &headers](const Http::HeaderEntry& cached_header) {
          // TODO(yosrym93): Investigate a better way to copy headers
          // to avoid copying the key twice
          absl::string_view key_view = cached_header.key().getStringView();
          Http::LowerCaseString key(std::string(key_view.data(), key_view.size()));
          if (!response_headers->get(key)) {
            response_headers->setCopy(key, cached_header.value().getStringView());
          }
          return Http::HeaderMap::Iterate::Continue;
        });

    cache_.updateHeaders(*lookup_, headers);
    if (encodeCachedResponse()) {
      // Stop encoding filter chain iteration until the data from cache is encoded
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    }
  }

  // Either a cache miss or cache entry is no longer valid
  // Check if the new response can be cached
  if (request_allows_inserts_ && CacheabilityUtils::isCacheableResponse(headers)) {
    // TODO(yosrym93): Add date internal header or metadata to cached responses and use it instead
    // of the date header
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting headers", *encoder_callbacks_);
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    insert_->insertHeaders(headers, end_stream);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (insert_) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::encodeHeaders inserting body", *encoder_callbacks_);
    // TODO(toddmgreer): Wait for the cache if necessary.
    insert_->insertBody(
        data, [](bool) {}, end_stream);
  }
  return Http::FilterDataStatus::Continue;
}

void CacheFilter::injectValidationHeaders(const Http::ResponseHeaderMapPtr& response_headers) {
  ASSERT(request_headers_,
         "CacheFilter must set request_headers_ in decodeHeaders before onHeaders is called.");
  const Http::HeaderEntry* etag_header = response_headers->get(Http::CustomHeaders::get().Etag);
  const Http::HeaderEntry* last_modified_header =
      response_headers->get(Http::CustomHeaders::get().LastModified);

  if (etag_header) {
    absl::string_view etag = etag_header->value().getStringView();
    request_headers_->setCopy(Http::LowerCaseString{"if-none-match"}, etag);
  }
  if (last_modified_header) {
    absl::string_view last_modified = last_modified_header->value().getStringView();
    request_headers_->setCopy(Http::LowerCaseString{"if-unmodified-since"}, last_modified);
  }
}

bool CacheFilter::encodeCachedResponse() {
  bool data_encoded = false;
  response_has_trailers_ = result_->has_trailers_;
  const bool end_stream = (result_->content_length_ == 0 && !response_has_trailers_);
  // TODO(toddmgreer): Calculate age per https://httpwg.org/specs/rfc7234.html#age.calculations
  result_->headers_->addReferenceKey(Http::Headers::get().Age, 0);

  // Set appropriate response flags and codes
  Http::StreamFilterCallbacks* callbacks_ =
      filter_state_ == FilterState::Encoding
          ? static_cast<Http::StreamFilterCallbacks*>(encoder_callbacks_)
          : static_cast<Http::StreamFilterCallbacks*>(decoder_callbacks_);
  callbacks_->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  callbacks_->streamInfo().setResponseCodeDetails(
      CacheResponseCodeDetails::get().ResponseFromCacheFilter);

  // If the filter is encoding, response headers and cached headers are merged in encodeHeaders
  // If the filter is decoding, we need to serve response headers from cache directly
  if (filter_state_ == FilterState::Decoding) {
    decoding_cache_hit_ = true;
    data_encoded = true;
    decoder_callbacks_->encodeHeaders(std::move(result_->headers_), end_stream);
  }

  if (result_->content_length_ > 0) {
    remaining_body_.emplace_back(0, result_->content_length_);
    getBody();
    data_encoded = true;
  } else if (response_has_trailers_) {
    lookup_->getTrailers(
        [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
    data_encoded = true;
  }

  return data_encoded;
}

void CacheFilter::onHeaders(LookupResult&& result) {
  // TODO(yosrym93): Handle request only-if-cached directive
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::UnsatisfiableRange:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // We don't yet return or support these codes.
  case CacheEntryStatus::RequiresValidation:
    // If a cache entry requires validation inject validadation headers in the request and let it
    // pass through as if no cache entry was found. If the cache entry was valid the response
    // status should be 304 (unmodified), and the cache entry will be injected in the response body
    injectValidationHeaders(result.headers_);
    cache_entry_validation_ = true;
    result_ = std::make_unique<LookupResult>(std::move(result));
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
    result_ = std::make_unique<LookupResult>(std::move(result));
    encodeCachedResponse();
  }
  // All switch case paths should lead to this line
  // Stored pointer to RequestHeaderMap is no longer needed
  request_headers_ = nullptr;
}

void CacheFilter::getBody() {
  ASSERT(!remaining_body_.empty(), "No reason to call getBody when there's no body to get.");
  lookup_->getBody(remaining_body_[0],
                   [this](Buffer::InstancePtr&& body) { onBody(std::move(body)); });
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

  if (filter_state_ == FilterState::Decoding) {
    decoder_callbacks_->encodeData(*body, end_stream);
  } else {
    // TODO(yosrym93): make sure that the streaming parameter to addEncodedData should
    // be true in this call
    encoder_callbacks_->addEncodedData(*body, true);
    if (end_stream) {
      // All data from cache encoded into the filter stream, continue encoding
      encoder_callbacks_->continueEncoding();
    }
  }

  if (!remaining_body_.empty()) {
    getBody();
  } else if (response_has_trailers_) {
    lookup_->getTrailers(
        [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  // Can be called during decoding if a valid cache hit is found
  // or during encoding if a cache entry was being validated
  if (filter_state_ == FilterState::Encoding) {
    Http::ResponseTrailerMap& response_trailers = encoder_callbacks_->addEncodedTrailers();
    response_trailers = std::move(*trailers);
    // All data from cache encoded into the filter stream, continue encoding
    encoder_callbacks_->continueEncoding();
  } else {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
