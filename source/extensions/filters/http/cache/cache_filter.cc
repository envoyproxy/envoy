#include "extensions/filters/http/cache/cache_filter.h"

#include "envoy/registry/registry.h"

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

using absl::string_view;
using absl::WrapUnique;
using Envoy::Http::FilterDataStatus;
using Envoy::Http::FilterHeadersStatus;
using Envoy::Http::HeaderEntry;
using Envoy::Http::HeaderMap;
using Envoy::Http::HeaderMapPtr;
using Envoy::Registry::FactoryRegistry;
using Envoy::Stats::Scope;
using std::function;
using std::make_shared;
using std::string;
using std::vector;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

bool isCacheableRequest(HeaderMap& headers) {
  const HeaderEntry* method = headers.Method();
  // TODO(toddmgreer) Also serve HEAD requests from cache.
  // TODO(toddmgreer) Check all the other cache-related headers.
  return ((method != nullptr) && method->value().getStringView() == "GET");
}

bool isCacheableResponse(HeaderMap& headers) {
  const HeaderEntry* cache_control = headers.CacheControl();
  // TODO(toddmgreer) fully check for cacheability. See for example
  // https://github.com/apache/incubator-pagespeed-mod/blob/master/pagespeed/kernel/http/caching_headers.h.
  return (cache_control != nullptr) &&
         (cache_control->value().getStringView().find("private") == string_view::npos);
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
                         const string&, Stats::Scope&, TimeSource& time_source)
    : time_source_(time_source), cache_(getCache(config)) {}

void CacheFilter::onDestroy() {
  lookup_ = nullptr;
  insert_ = nullptr;
}

FilterHeadersStatus CacheFilter::decodeHeaders(HeaderMap& headers, bool) {
  if (!isCacheableRequest(headers)) {
    return FilterHeadersStatus::Continue;
  }
  ASSERT(decoder_callbacks_);
  lookup_ = cache_.makeLookupContext(LookupRequest(headers, time_source_.systemTime()));
  ASSERT(lookup_);

  CacheFilterSharedPtr self = shared_from_this();
  lookup_->getHeaders([self](LookupResult&& result) { onHeadersAsync(self, std::move(result)); });
  return FilterHeadersStatus::StopIteration;
}

FilterHeadersStatus CacheFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  if (lookup_ && isCacheableResponse(headers)) {
    insert_ = cache_.makeInsertContext(std::move(lookup_));
    insert_->insertHeaders(headers, end_stream);
  }
  return FilterHeadersStatus::Continue;
}

FilterDataStatus CacheFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (insert_) {
    // TODO(toddmgreer) Wait for the cache if necessary.
    insert_->insertBody(
        data, [](bool) {}, end_stream);
  }
  return FilterDataStatus::Continue;
}

void CacheFilter::onOkHeaders(HeaderMapPtr&& headers, vector<AdjustedByteRange>&& response_ranges,
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
    lookup_->getTrailers([self = shared_from_this()](HeaderMapPtr&& trailers) {
      onTrailersAsync(self, std::move(trailers));
    });
  }
}

void CacheFilter::onUnusableHeaders() {
  if (lookup_) {
    decoder_callbacks_->continueDecoding();
  }
}

void CacheFilter::onHeadersAsync(CacheFilterSharedPtr self, LookupResult&& result) {
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
      self->onOkHeaders(WrapUnique(headers), std::move(response_ranges), content_length,
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

void CacheFilter::onBodyAsync(CacheFilterSharedPtr self, Buffer::InstancePtr&& body) {
  self->post([self, body = body.release()] { self->onBody(WrapUnique(body)); });
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
    lookup_->getTrailers([self = shared_from_this()](HeaderMapPtr&& trailers) {
      onTrailersAsync(self, std::move(trailers));
    });
  }
}

void CacheFilter::onTrailers(HeaderMapPtr&& trailers) {
  if (lookup_) {
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
}

void CacheFilter::onTrailersAsync(CacheFilterSharedPtr self, HeaderMapPtr&& trailers) {
  self->post([self, trailers = trailers.release()] { self->onTrailers(WrapUnique(trailers)); });
}

void CacheFilter::post(function<void()> f) const {
  decoder_callbacks_->dispatcher().post(std::move(f));
}
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
