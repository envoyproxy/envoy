#pragma once

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/service/tap/v2alpha/common.pb.h"

#include "extensions/common/tap/tap.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

/**
 * Per-request/stream HTTP tap implementation. Abstractly handles all request lifecycle events in
 * order to tap if the configuration matches.
 */
class HttpPerRequestTapper {
public:
  virtual ~HttpPerRequestTapper() = default;

  /**
   * Called when request headers are received.
   */
  virtual void onRequestHeaders(const Http::HeaderMap& headers) PURE;

  /**
   * Called when request trailers are received.
   */
  virtual void onRequestTrailers(const Http::HeaderMap& trailers) PURE;

  /**
   * Called when response headers are received.
   */
  virtual void onResponseHeaders(const Http::HeaderMap& headers) PURE;

  /**
   * Called when response trailers are received.
   */
  virtual void onResponseTrailers(const Http::HeaderMap& headers) PURE;

  /**
   * Called when the request is being destroyed and is being logged.
   * @return whether the request was tapped or not.
   */
  virtual bool onDestroyLog(const Http::HeaderMap* request_headers,
                            const Http::HeaderMap* request_trailers,
                            const Http::HeaderMap* response_headers,
                            const Http::HeaderMap* response_trailers) PURE;
};

using HttpPerRequestTapperPtr = std::unique_ptr<HttpPerRequestTapper>;

/**
 * Abstract HTTP tap configuration.
 */
class HttpTapConfig : public Extensions::Common::Tap::TapConfig {
public:
  /**
   * @return a new per-request HTTP tapper which is used to handle tapping of a discrete request.
   * @param stream_id supplies the owning HTTP stream ID.
   */
  virtual HttpPerRequestTapperPtr createPerRequestTapper(uint64_t stream_id) PURE;
};

using HttpTapConfigSharedPtr = std::shared_ptr<HttpTapConfig>;

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
