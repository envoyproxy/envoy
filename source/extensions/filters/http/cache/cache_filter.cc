#include "extensions/filters/http/cache/cache_filter.h"

#include "envoy/registry/registry.h"

#include "common/http/headers.h"

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

bool isCacheableRequest(Http::HeaderMap& headers) {
  const Http::HeaderEntry* method = headers.Method();
  // TODO(toddmgreer) Also serve HEAD requests from cache.
  // TODO(toddmgreer) Check all the other cache-related headers.
  return ((method != nullptr) &&
          method->value().getStringView() == Http::Headers::get().MethodValues.Head);
}

bool isCacheableResponse(Http::HeaderMap& headers) {
  const Http::HeaderEntry* cache_control = headers.CacheControl();
  // TODO(toddmgreer) fully check for cacheability. See for example
  // https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/caching_headers.h.
  if (cache_control) {
    return !StringUtil::caseFindToken(cache_control->value().getStringView(), ",",
                                      Http::Headers::get().CacheControlValues.Private);
  }
  return false;
}

HttpCache& getCache(const envoy::config::filter::http::cache::v2alpha::Cache& config) {
  HttpCacheFactory* factory =
      Registry::FactoryRegistry<HttpCacheFactory>::getFactory(config.name());
  if (!factory) {
    throw EnvoyException(
        fmt::format("Didn't find a registered HttpCacheFactory for '{}'", config.name()));
  }
  return factory->getCache();
}
} // namespace

CacheFilter::CacheFilter(const envoy::config::filter::http::cache::v2alpha::Cache& config,
                         const std::string&, Stats::Scope&, TimeSource& time_source)
    : time_source_(time_source), cache_(getCache(config)) {}

void CacheFilter::onDestroy() {
  lookup_ = nullptr;
  insert_ = nullptr;
}

Http::FilterHeadersStatus CacheFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  if (!isCacheableRequest(headers)) {
    return Http::FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);
  lookup_ = cache_.makeLookupContext(LookupRequest(headers, time_source_.systemTime()));
  ASSERT(lookup_);

  CacheFilterSharedPtr self = shared_from_this();
  lookup_->getHeaders([self](LookupResult&& result) { onHeadersAsync(self, std::move(result)); });
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus CacheFilter::encodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  if (lookup_ && isCacheableResponse(headers)) {
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    insert_->insertHeaders(headers, end_stream);
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (insert_) {
    // TODO(toddmgreer) Wait for the cache if necessary.
    insert_->insertBody(
        data, [](bool) {}, end_stream);
  }
  return Http::FilterDataStatus::Continue;
}

void CacheFilter::onOkHeaders(Http::HeaderMapPtr&& headers,
                              std::vector<AdjustedByteRange>&& response_ranges,
                              uint64_t content_length, bool has_trailers) {
  if (!lookup_) {
    return;
  }
  response_has_trailers_ = has_trailers;
  const bool end_stream = (content_length == 0 && !response_has_trailers_);
  decoder_callbacks_->encodeHeaders(std::move(headers), end_stream);
  if (end_stream) {
    return;
  }
  if (content_length > 0) {
    remaining_body_ = std::move(response_ranges);
    // TODO(toddmgreer) handle multi-range requests.
    ASSERT(remaining_body_.size() <= 1);
    if (remaining_body_.empty()) {
      remaining_body_.emplace_back(0, content_length - 1);
    }
    getBody();
  } else {
    lookup_->getTrailers([self = shared_from_this()](Http::HeaderMapPtr&& trailers) {
      onTrailersAsync(self, std::move(trailers));
    });
  }
}

void CacheFilter::onUnusableHeaders() {
  if (lookup_) {
    decoder_callbacks_->continueDecoding();
  }
}

void CacheFilter::onHeadersAsync(const CacheFilterSharedPtr& self, LookupResult&& result) {
  switch (result.cache_entry_status) {
  case CacheEntryStatus::RequiresValidation:
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::UnsatisfiableRange:
    ASSERT(false); // We don't yet return or support these codes.
    // FALLTHROUGH
  case CacheEntryStatus::Unusable: {
    self->post([self] { self->onUnusableHeaders(); });
    return;
  }
  case CacheEntryStatus::Ok:
    self->post([self, headers = result.headers.release(),
                response_ranges = std::move(result.response_ranges),
                content_length = result.content_length,
                has_trailers = result.has_trailers]() mutable {
      self->onOkHeaders(absl::WrapUnique(headers), std::move(response_ranges), content_length,
                        has_trailers);
    });
  }
}

void CacheFilter::getBody() {
  ASSERT(!remaining_body_.empty());
  CacheFilterSharedPtr self = shared_from_this();
  lookup_->getBody(remaining_body_[0],
                   [self](Buffer::InstancePtr&& body) { self->onBody(std::move(body)); });
}

void CacheFilter::onBodyAsync(const CacheFilterSharedPtr& self, Buffer::InstancePtr&& body) {
  self->post([self, body = body.release()] { self->onBody(absl::WrapUnique(body)); });
}

void CacheFilter::onBody(Buffer::InstancePtr&& body) {
  if (!lookup_) {
    return;
  }
  ASSERT(!remaining_body_.empty());
  if (!body) {
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
    lookup_->getTrailers([self = shared_from_this()](Http::HeaderMapPtr&& trailers) {
      onTrailersAsync(self, std::move(trailers));
    });
  }
}

void CacheFilter::onTrailers(Http::HeaderMapPtr&& trailers) {
  if (lookup_) {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
}

void CacheFilter::onTrailersAsync(const CacheFilterSharedPtr& self, Http::HeaderMapPtr&& trailers) {
  self->post(
      [self, trailers = trailers.release()] { self->onTrailers(absl::WrapUnique(trailers)); });
}

void CacheFilter::post(std::function<void()> f) const {
  decoder_callbacks_->dispatcher().post(std::move(f));
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
