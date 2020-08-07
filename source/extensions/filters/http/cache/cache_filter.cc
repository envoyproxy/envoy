#include "extensions/filters/http/cache/cache_filter.h"

#include "common/http/headers.h"

#include "extensions/filters/http/cache/cacheability_utils.h"

#include "absl/strings/str_cat.h"
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
  lookup_->getHeaders([this](LookupResult&& result) { onHeaders(std::move(result)); });
  if (state_ == GetHeadersState::GetHeadersResultUnusable) {
    // onHeaders has already been called, and no usable cache entry was found--continue iteration.
    return Http::FilterHeadersStatus::Continue;
  }
  // onHeaders hasn't been called yet--stop iteration to wait for it, and tell it that we stopped
  // iteration.
  state_ = GetHeadersState::FinishedGetHeadersCall;
  return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                     bool end_stream) {
  // If lookup_ is null, the request wasn't cacheable, so the response isn't either.
  if (lookup_ && request_allows_inserts_ && CacheabilityUtils::isCacheableResponse(headers)) {
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

void CacheFilter::onHeaders(LookupResult&& result) {
  // TODO(yosrym93): Handle request only-if-cached directive
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::FoundNotModified:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // We don't yet return or support these codes.
  case CacheEntryStatus::RequiresValidation:
    // Cache entries that require validation are treated as unusable entries
    // until validation is implemented
    // TODO(yosrym93): Implement response validation
  case CacheEntryStatus::Unusable:
    if (state_ == GetHeadersState::FinishedGetHeadersCall) {
      // decodeHeader returned Http::FilterHeadersStatus::StopAllIterationAndWatermark--restart it
      decoder_callbacks_->continueDecoding();
    } else {
      // decodeHeader hasn't yet returned--tell it to return Http::FilterHeadersStatus::Continue.
      state_ = GetHeadersState::GetHeadersResultUnusable;
    }
    return;
  case CacheEntryStatus::NotSatisfiableRange: {
    Http::ResponseHeaderMapPtr response_headers = std::move(result.headers_);
    response_headers->setStatus(static_cast<uint64_t>(Http::Code::RangeNotSatisfiable));
    response_headers->setContentLength(0);
    response_headers->addCopy(Http::Headers::get().ContentRange,
                              absl::StrCat("bytes */", result.content_length_));
    response_has_trailers_ = result.has_trailers_;
    addResponseAge(response_headers);
    // There is no body to send, so we decide to end the stream only based on having trailers.
    sendHeaders(std::move(response_headers), !response_has_trailers_);
    if (response_has_trailers_) {
      lookup_->getTrailers(
          [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
    }
    return;
  }
  case CacheEntryStatus::SatisfiableRange: {
    if (result.response_ranges_.size() == 1) {
      Http::ResponseHeaderMapPtr response_headers = std::move(result.headers_);
      response_headers->setStatus(static_cast<uint64_t>(Http::Code::PartialContent));
      response_headers->setContentLength(result.response_ranges_[0].length());
      response_headers->addCopy(Http::Headers::get().ContentRange,
                                absl::StrCat("bytes ", result.response_ranges_[0].begin(), "-",
                                             result.response_ranges_[0].end() - 1, "/",
                                             result.content_length_));
      response_has_trailers_ = result.has_trailers_;
      addResponseAge(response_headers);
      // There's always a body to send (i.e., the satisfiable range), so end_stream is false.
      sendHeaders(std::move(response_headers), false);
      remaining_ranges_ = std::move(result.response_ranges_);
      getBody();
      return;
    }
    // Multi-part responses are not supported, and they will be treated as a usual 200 response.
    // A possible way to achieve that would be to move all ranges to remaining_body, and
    // add logic inside '::onBody' to interleave the body bytes with sub-headers and separator
    // string for each part. Would need to keep track if the current range is over or not to know
    // when to insert the separator, and calculate the length based on length of ranges + extra
    // headers and separators.
    ABSL_FALLTHROUGH_INTENDED;
  }
  case CacheEntryStatus::Ok:
    response_has_trailers_ = result.has_trailers_;
    const bool end_stream = (result.content_length_ == 0 && !response_has_trailers_);
    addResponseAge(result.headers_);
    sendHeaders(std::move(result.headers_), end_stream);
    if (end_stream) {
      return;
    }
    if (result.content_length_ > 0) {
      remaining_ranges_.emplace_back(0, result.content_length_);
      getBody();
    } else {
      lookup_->getTrailers(
          [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
    }
  }
}

void CacheFilter::getBody() {
  ASSERT(!remaining_ranges_.empty(), "No reason to call getBody when there's no body to get.");
  lookup_->getBody(remaining_ranges_[0],
                   [this](Buffer::InstancePtr&& body) { onBody(std::move(body)); });
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
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
    decoder_callbacks_->resetStream();
    return;
  }

  const bool end_stream = remaining_ranges_.empty() && !response_has_trailers_;
  decoder_callbacks_->encodeData(*body, end_stream);
  if (!remaining_ranges_.empty()) {
    getBody();
  } else if (response_has_trailers_) {
    lookup_->getTrailers(
        [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  decoder_callbacks_->encodeTrailers(std::move(trailers));
}

void CacheFilter::addResponseAge(Http::ResponseHeaderMapPtr& headers) {
  // TODO(toddmgreer): Calculate age per https://httpwg.org/specs/rfc7234.html#age.calculations
  headers->addReferenceKey(Http::Headers::get().Age, 0);
}

void CacheFilter::sendHeaders(Http::ResponseHeaderMapPtr&& headers, const bool end_stream) {
  decoder_callbacks_->streamInfo().setResponseFlag(
      StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  decoder_callbacks_->streamInfo().setResponseCodeDetails(
      CacheResponseCodeDetails::get().ResponseFromCacheFilter);
  decoder_callbacks_->encodeHeaders(std::move(headers), end_stream);
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
