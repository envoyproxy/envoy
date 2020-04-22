#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/cache/v3alpha/cache.pb.h"

#include "common/common/logger.h"

#include "extensions/filters/http/cache/http_cache.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

/**
 * A filter that caches responses and attempts to satisfy requests from cache.
 */
class CacheFilter : public Http::PassThroughFilter,
                    public Logger::Loggable<Logger::Id::cache_filter> {
public:
  CacheFilter(const envoy::extensions::filters::http::cache::v3alpha::CacheConfig& config,
              const std::string& stats_prefix, Stats::Scope& scope, TimeSource& time_source,
              HttpCache& http_cache);
  // Http::StreamFilterBase
  void onDestroy() override;
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override;

private:
  void getBody();
  void onHeaders(LookupResult&& result);
  void onBody(Buffer::InstancePtr&& body);
  void onTrailers(Http::ResponseTrailerMapPtr&& trailers);

  // These don't require private access, but are members per envoy convention.
  static bool isCacheableRequest(Http::RequestHeaderMap& headers);
  static bool isCacheableResponse(Http::ResponseHeaderMap& headers);

  TimeSource& time_source_;
  HttpCache& cache_;
  LookupContextPtr lookup_;
  InsertContextPtr insert_;

  // Tracks what body bytes still need to be read from the cache. This is
  // currently only one Range, but will expand when full range support is added. Initialized by
  // onOkHeaders.
  std::vector<AdjustedByteRange> remaining_body_;

  // True if the response has trailers.
  // TODO(toddmgreer): cache trailers.
  bool response_has_trailers_;

  // Used for coordinating between decodeHeaders and onHeaders.
  enum class GetHeadersState { Initial, FinishedGetHeadersCall, GetHeadersResultUnusable };
  GetHeadersState state_ = GetHeadersState::Initial;
};

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
