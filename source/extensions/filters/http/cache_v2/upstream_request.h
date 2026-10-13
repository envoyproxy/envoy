#pragma once

#include "source/extensions/filters/http/cache_v2/http_source.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

class CacheFilterStatsProvider;

class UpstreamRequest : public HttpSource {
public:
  virtual void sendHeaders(Http::RequestHeaderMapPtr headers) PURE;
};

using UpstreamRequestPtr = std::unique_ptr<UpstreamRequest>;

// UpstreamRequest acts as a bridge between the "pull" operations preferred by
// the cache filter (getHeaders/getBody/getTrailers) and the "push" operations
// preferred by most of envoy (encodeHeaders etc. being called by the source).
//
// In order to bridge the two, UpstreamRequest must act as a buffer; on a get*
// request it calls back only when the buffer has [some of] the requested data
// in it; if the buffer gets overfull, watermark events are triggered on the
// upstream. The client side should only send get* requests when it is ready for
// more data, so the downstream is automatically resilient to OOM.
// TODO(#33319): AsyncClient::Stream does not currently support watermark events.
class UpstreamRequestFactory {
public:
  virtual UpstreamRequestPtr
  create(const std::shared_ptr<const CacheFilterStatsProvider> stats_provider) PURE;
  virtual ~UpstreamRequestFactory() = default;
};

using UpstreamRequestFactoryPtr = std::unique_ptr<UpstreamRequestFactory>;

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
