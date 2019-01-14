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
   * Called when response headers are received.
   */
  virtual void onResponseHeaders(const Http::HeaderMap& headers) PURE;

  /**
   * Called when the request is being destroyed and is being logged.
   * @return whether the request was tapped or not.
   */
  virtual bool onDestroyLog(const Http::HeaderMap* request_headers,
                            const Http::HeaderMap* response_headers) PURE;
};

using HttpPerRequestTapperPtr = std::unique_ptr<HttpPerRequestTapper>;

/**
 * Abstract HTTP tap configuration.
 */
class HttpTapConfig {
public:
  virtual ~HttpTapConfig() = default;

  /**
   * @return a new per-request HTTP tapper which is used to handle tapping of a discrete request.
   */
  virtual HttpPerRequestTapperPtr createPerRequestTapper() PURE;
};

using HttpTapConfigSharedPtr = std::shared_ptr<HttpTapConfig>;

/**
 * Configuration factory for the HTTP tap filter.
 */
class HttpTapConfigFactory {
public:
  virtual ~HttpTapConfigFactory() = default;

  /**
   * @return a new configuration given a raw tap service config proto. See
   * Extensions::Common::Tap::ExtensionConfig::newTapConfig() for param info.
   */
  virtual HttpTapConfigSharedPtr
  createHttpConfigFromProto(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                            Extensions::Common::Tap::Sink* admin_streamer) PURE;
};

using HttpTapConfigFactoryPtr = std::unique_ptr<HttpTapConfigFactory>;

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
