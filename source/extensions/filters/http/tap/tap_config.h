#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"
#include "envoy/http/header_map.h"

#include "source/extensions/common/tap/tap.h"

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
  virtual void onRequestHeaders(const Http::RequestHeaderMap& headers) PURE;

  /**
   * Called when request body is received.
   */
  virtual void onRequestBody(const Buffer::Instance& data) PURE;

  /**
   * Called when request trailers are received.
   */
  virtual void onRequestTrailers(const Http::RequestTrailerMap& trailers) PURE;

  /**
   * Called when response headers are received.
   */
  virtual void onResponseHeaders(const Http::ResponseHeaderMap& headers) PURE;

  /**
   * Called when response body is received.
   */
  virtual void onResponseBody(const Buffer::Instance& data) PURE;

  /**
   * Called when response trailers are received.
   */
  virtual void onResponseTrailers(const Http::ResponseTrailerMap& headers) PURE;

  /**
   * Called when the request is being destroyed and is being logged.
   * @return whether the request was tapped or not.
   */
  virtual bool onDestroyLog() PURE;
};

using HttpPerRequestTapperPtr = std::unique_ptr<HttpPerRequestTapper>;

/**
 * Abstract HTTP tap configuration.
 */
class HttpTapConfig : public virtual Extensions::Common::Tap::TapConfig {
public:
  /**
   * @return a new per-request HTTP tapper which is used to handle tapping of a discrete request.
   * @param tap_config provides http tap config
   * @param stream_id supplies the owning HTTP stream ID.
   */
  virtual HttpPerRequestTapperPtr
  createPerRequestTapper(const envoy::extensions::filters::http::tap::v3::Tap& tap_config,
                         uint64_t stream_id, OptRef<const Network::Connection> connection) PURE;

  /**
   * @return time source to use for timestamp
   */
  virtual TimeSource& timeSource() const PURE;
};

using HttpTapConfigSharedPtr = std::shared_ptr<HttpTapConfig>;

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
