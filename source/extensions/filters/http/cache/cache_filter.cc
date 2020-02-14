#include "extensions/filters/http/cache/cache_filter.h"

#include "common/http/headers.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

bool CacheFilter::isCacheableRequest(Http::HeaderMap& headers) {
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

bool CacheFilter::isCacheableResponse(Http::HeaderMap& headers) {
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
  // Clear decoder_callbacks_ so any pending callbacks will see that this filter is no longer
  // active().
  decoder_callbacks_ = nullptr;
  if (lookup_) {
    lookup_->onDestroy();
  }
  if (insert_) {
    insert_->onDestroy();
  }
}

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders: {}", *decoder_callbacks_, headers);
  if (!isCacheableRequest(headers)) {
    ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders ignoring uncacheable request: {}",
                     *decoder_callbacks_, headers);
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);
  lookup_ = cache_.makeLookupContext(LookupRequest(headers, time_source_.systemTime()));
  ASSERT(lookup_);

  ENVOY_STREAM_LOG(debug, "CacheFilter::decodeHeaders starting lookup", *decoder_callbacks_);
  lookup_->getHeaders([this](LookupResult&& result) { onHeadersAsync(std::move(result)); });
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
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
  if (!active()) {
    return;
  }
  switch (result.cache_entry_status_) {
  case CacheEntryStatus::RequiresValidation:
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::UnsatisfiableRange:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE; // We don't yet return or support these codes.
  case CacheEntryStatus::Unusable:
    decoder_callbacks_->continueDecoding();
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
          [this](Http::HeaderMapPtr&& trailers) { onTrailersAsync(std::move(trailers)); });
    }
  }
}

void CacheFilter::onHeadersAsync(LookupResult&& result) {
  post([this, status = result.cache_entry_status_, headers = result.headers_.release(),
        response_ranges = std::move(result.response_ranges_),
        content_length = result.content_length_, has_trailers = result.has_trailers_] {
    onHeaders(LookupResult{status, absl::WrapUnique(headers), content_length, response_ranges,
                           has_trailers});
  });
}

void CacheFilter::getBody() {
  ASSERT(!remaining_body_.empty(), "No reason to call getBody when there's no body to get.");
  lookup_->getBody(remaining_body_[0],
                   [this](Buffer::InstancePtr&& body) { onBody(std::move(body)); });
}

void CacheFilter::onBodyAsync(Buffer::InstancePtr&& body) {
  post([this, body = body.release()] { onBody(absl::WrapUnique(body)); });
}

// TODO(toddmgreer): Handle downstream backpressure.
void CacheFilter::onBody(Buffer::InstancePtr&& body) {
  if (!active()) {
    return;
  }
  if (remaining_body_.empty()) {
    ASSERT(false, "CacheFilter doesn't call getBody unless there's more body to get, so this is a "
                  "bogus callback.");
    decoder_callbacks_->resetStream();
    return;
  }

  if (!body) {
    ASSERT(false, "Cache said it had a body, but isn't giving it to us.");
    decoder_callbacks_->resetStream();
    return;
  }

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
        [this](Http::HeaderMapPtr&& trailers) { onTrailersAsync(std::move(trailers)); });
  }
}

void CacheFilter::onTrailers(Http::HeaderMapPtr&& trailers) {
  if (active()) {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
}

void CacheFilter::onTrailersAsync(Http::HeaderMapPtr&& trailers) {
  post([this, trailers = trailers.release()] { onTrailers(absl::WrapUnique(trailers)); });
}

void CacheFilter::post(std::function<void()> f) const {
  decoder_callbacks_->dispatcher().post(std::move(f));
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
