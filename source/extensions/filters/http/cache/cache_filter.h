#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/http/cache/v2alpha/cache.pb.h"

#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 */
class CacheFilter;
using CacheFilterSharedPtr = std::shared_ptr<CacheFilter>;
class CacheFilter : public Http::PassThroughFilter,
                    public std::enable_shared_from_this<CacheFilter> {
public:
  // Throws ProtoValidationException if no registered HttpCacheFactory for config.name.
  static CacheFilterSharedPtr make(const envoy::config::filter::http::cache::v2alpha::Cache& config,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   TimeSource& time_source) {
    return std::shared_ptr<CacheFilter>(new CacheFilter(config, stats_prefix, scope, time_source));
  }
  // Http::StreamFilterBase
  void onDestroy() override;
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  // Throws EnvoyException if no registered HttpCacheFactory for config.name.
  // Constructor is private to enforce enable_shared_from_this's requirement that this must be owned
  // by a shared_ptr.
  CacheFilter(const envoy::config::filter::http::cache::v2alpha::Cache& config,
              const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source);

  void getBody();
  void onOkHeaders(Http::HeaderMapPtr&& headers, std::vector<AdjustedByteRange>&& response_ranges,
                   uint64_t content_length, bool has_trailers);
  void onUnusableHeaders();
  void onBody(Buffer::InstancePtr&& body);
  void onTrailers(Http::HeaderMapPtr&& trailers);
  static void onHeadersAsync(const CacheFilterSharedPtr& self, LookupResult&& result);
  static void onBodyAsync(const CacheFilterSharedPtr& self, Buffer::InstancePtr&& body);
  static void onTrailersAsync(const CacheFilterSharedPtr& self, Http::HeaderMapPtr&& trailers);
  void post(std::function<void()> f) const;

  TimeSource& time_source_;
  HttpCache& cache_;
  LookupContextPtr lookup_;
  InsertContextPtr insert_;

  // Tracks what body bytes still need to be read from the cache. This is
  // currently only one Range, but will expand when full range support is added.
  std::vector<AdjustedByteRange> remaining_body_;

  // True if the response has trailers.
  // TODO(toddmgreer) cache trailers.
  bool response_has_trailers_;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
