#include "extensions/filters/http/cache/cache_filter.h"

#include "common/http/headers.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

bool CacheFilter::isCacheableRequest(Http::RequestHeaderMap& headers) {
  const Http::HeaderEntry* method = headers.Method();
  const Http::HeaderEntry* forwarded_proto = headers.ForwardedProto();
  const Http::HeaderValues& header_values = Http::Headers::get();
  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // TODO(toddmgreer): Check all the other cache-related headers.
  return method && forwarded_proto && headers.Path() && headers.Host() &&
         (method->value() == header_values.MethodValues.Get) &&
         (forwarded_proto->value() == header_values.SchemeValues.Http ||
          forwarded_proto->value() == header_values.SchemeValues.Https);
}

bool CacheFilter::isCacheableResponse(Http::ResponseHeaderMap& headers) {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  // TODO(toddmgreer): fully check for cacheability. See for example
  // https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/caching_headers.h.
  if (cache_control) {
    return !StringUtil::caseFindToken(cache_control->value().getStringView(), ",",
                                      Http::Headers::get().CacheControlValues.Private);
  }
  return false;
}

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
  if (!isCacheableRequest(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders ignoring uncacheable request: {}",
                     *decoder_callbacks_, headers);
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);
  lookup_ = cache_.makeLookupContext(LookupRequest(headers, time_source_.systemTime()));
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
  if (lookup_ && isCacheableResponse(headers)) {
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
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::RequiresValidation:
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::UnsatisfiableRange:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // We don't yet return or support these codes.
  case CacheEntryStatus::Unusable:
    if (state_ == GetHeadersState::FinishedGetHeadersCall) {
      // decodeHeader returned Http::FilterHeadersStatus::StopAllIterationAndWatermark--restart it
      decoder_callbacks_->continueDecoding();
    } else {
      // decodeHeader hasn't yet returned--tell it to return Http::FilterHeadersStatus::Continue.
      state_ = GetHeadersState::GetHeadersResultUnusable;
    }
    return;
  case CacheEntryStatus::Ok:
    response_has_trailers_ = result.has_trailers_;
    const bool end_stream = (result.content_length_ == 0 && !response_has_trailers_);
    // TODO(toddmgreer): Calculate age per https://httpwg.org/specs/rfc7234.html#age.calculations
    result.headers_->addReferenceKey(Http::Headers::get().Age, 0);
    decoder_callbacks_->encodeHeaders(std::move(result.headers_), end_stream);
    if (end_stream) {
      return;
    }
    if (result.content_length_ > 0) {
      remaining_body_.emplace_back(0, result.content_length_);
      getBody();
    } else {
      lookup_->getTrailers(
          [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
    }
  }
}

void CacheFilter::getBody() {
  ASSERT(!remaining_body_.empty(), "No reason to call getBody when there's no body to get.");
  lookup_->getBody(remaining_body_[0],
                   [this](Buffer::InstancePtr&& body) { onBody(std::move(body)); });
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
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
    decoder_callbacks_->resetStream();
    return;
  }

  const bool end_stream = remaining_body_.empty() && !response_has_trailers_;
  decoder_callbacks_->encodeData(*body, end_stream);
  if (!remaining_body_.empty()) {
    getBody();
  } else if (response_has_trailers_) {
    lookup_->getTrailers(
        [this](Http::ResponseTrailerMapPtr&& trailers) { onTrailers(std::move(trailers)); });
  }
}

void CacheFilter::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  decoder_callbacks_->encodeTrailers(std::move(trailers));
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
