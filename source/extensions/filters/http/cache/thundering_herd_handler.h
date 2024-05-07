#pragma once

#include "envoy/extensions/filters/http/cache/v3/cache.pb.h"
#include "envoy/http/filter.h"
#include "source/extensions/filters/http/cache/http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class CacheFilter;

class ThunderingHerdRetryInterface {
public:
  virtual void retryHeaders(Http::RequestHeaderMap& request_headers) PURE;
  virtual ~ThunderingHerdRetryInterface() = default;
};

class ThunderingHerdHandler {
public:
  static std::shared_ptr<ThunderingHerdHandler>
  create(const envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler&
             config);

  virtual void handleUpstreamRequest(std::weak_ptr<ThunderingHerdRetryInterface> weak_filter,
                                     Http::StreamDecoderFilterCallbacks* decoder_callbacks,
                                     const Key& key, Http::RequestHeaderMap& request_headers) PURE;
  virtual void handleInsertFinished(const Key& key, bool insert_succeeded) PURE;

  virtual ~ThunderingHerdHandler() = default;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
