#pragma once

#include "extensions/filters/http/cache/cache_headers_utils.h"
#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/cache/simple_http_cache/simple_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Wrapper for SimpleHttpCache that delays the onHeaders/onBody/onTrailers callbacks from
// getHeaders/getBody/getTrailers; for verifying that CacheFilter works correctly whether the
// callbacks happen immediately, or after getHeaders/getBody/getTrailers return
// Used to test the synchronization made using get_headers_state_ &  encode_cached_response_state_
class DelayedCache : public SimpleHttpCache {
public:
  // HttpCache
  LookupContextPtr makeLookupContext(LookupRequest&& request) override {
    return std::make_unique<DelayedLookupContext>(
        SimpleHttpCache::makeLookupContext(std::move(request)),
        DelayedCallbacks{delayed_headers_cb_, delayed_body_cb_, delayed_trailers_cb_});
  }
  InsertContextPtr makeInsertContext(LookupContextPtr&& lookup_context) override {
    return SimpleHttpCache::makeInsertContext(
        std::move(dynamic_cast<DelayedLookupContext&>(*lookup_context).context_));
  }

  std::function<void()> delayed_headers_cb_, delayed_body_cb_, delayed_trailers_cb_;

private:
  struct DelayedCallbacks {
    std::function<void()>&headers_cb_, &body_cb_, &trailers_cb_;
  };
  class DelayedLookupContext : public LookupContext {
  public:
    DelayedLookupContext(LookupContextPtr&& context, DelayedCallbacks delayed_callbacks)
        : context_(std::move(context)), delayed_callbacks_(delayed_callbacks) {}
    void getHeaders(LookupHeadersCallback&& cb) override {
      delayed_callbacks_.headers_cb_ = [this, cb]() mutable {
        context_->getHeaders(std::move(cb));
      };
    }
    void getBody(const AdjustedByteRange& range, LookupBodyCallback&& cb) override {
      delayed_callbacks_.body_cb_ = [this, cb, range]() mutable {
        context_->getBody(range, std::move(cb));
      };
    }
    void getTrailers(LookupTrailersCallback&& cb) override {
      delayed_callbacks_.trailers_cb_ = [this, cb]() mutable {
        context_->getTrailers(std::move(cb));
      };
    }

    LookupContextPtr context_;
    DelayedCallbacks delayed_callbacks_;
  };
};

std::ostream& operator<<(std::ostream& os, const RequestCacheControl& request_cache_control) {
  std::string s = "{";
  s += request_cache_control.must_validate_ ? "must_validate, " : "";
  s += request_cache_control.no_store_ ? "no_store, " : "";
  s += request_cache_control.no_transform_ ? "no_transform, " : "";
  s += request_cache_control.only_if_cached_ ? "only_if_cached, " : "";

  s += request_cache_control.max_age_.has_value()
           ? "max-age=" + std::to_string(request_cache_control.max_age_.value().count()) + ", "
           : "";
  s += request_cache_control.min_fresh_.has_value()
           ? "min-fresh=" + std::to_string(request_cache_control.min_fresh_.value().count()) + ", "
           : "";
  s += request_cache_control.max_stale_.has_value()
           ? "max-stale=" + std::to_string(request_cache_control.max_stale_.value().count()) + ", "
           : "";

  // Remove any extra ", " at the end
  if (s.size() > 1) {
    s.pop_back();
    s.pop_back();
  }

  s += "}";
  return os << s;
}

std::ostream& operator<<(std::ostream& os, const ResponseCacheControl& response_cache_control) {
  std::string s = "{";
  s += response_cache_control.must_validate_ ? "must_validate, " : "";
  s += response_cache_control.no_store_ ? "no_store, " : "";
  s += response_cache_control.no_transform_ ? "no_transform, " : "";
  s += response_cache_control.no_stale_ ? "no_stale, " : "";
  s += response_cache_control.is_public_ ? "public, " : "";

  s += response_cache_control.max_age_.has_value()
           ? "max-age=" + std::to_string(response_cache_control.max_age_.value().count()) + ", "
           : "";

  // Remove any extra ", " at the end
  if (s.size() > 1) {
    s.pop_back();
    s.pop_back();
  }

  s += "}";
  return os << s;
}

std::ostream& operator<<(std::ostream& os, CacheEntryStatus status) {
  switch (status) {
  case CacheEntryStatus::Ok:
    return os << "Ok";
  case CacheEntryStatus::Unusable:
    return os << "Unusable";
  case CacheEntryStatus::RequiresValidation:
    return os << "RequiresValidation";
  case CacheEntryStatus::FoundNotModified:
    return os << "FoundNotModified";
  case CacheEntryStatus::SatisfiableRange:
    return os << "SatisfiableRange";
  case CacheEntryStatus::NotSatisfiableRange:
    return os << "NotSatisfiableRange";
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

std::ostream& operator<<(std::ostream& os, const AdjustedByteRange& range) {
  return os << "[" << range.begin() << "," << range.end() << ")";
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy