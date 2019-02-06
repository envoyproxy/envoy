#pragma once

#include "envoy/http/header_map.h"

#include "common/common/logger.h"

#include "extensions/common/tap/tap_config_base.h"
#include "extensions/filters/http/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class HttpTapConfigImpl : public Extensions::Common::Tap::TapConfigBaseImpl,
                          public HttpTapConfig,
                          public std::enable_shared_from_this<HttpTapConfigImpl> {
public:
  HttpTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Extensions::Common::Tap::Sink* admin_streamer);

  // TapFilter::HttpTapConfig
  HttpPerRequestTapperPtr createPerRequestTapper(uint64_t stream_id) override;
};

using HttpTapConfigImplSharedPtr = std::shared_ptr<HttpTapConfigImpl>;

class HttpPerRequestTapperImpl : public HttpPerRequestTapper, Logger::Loggable<Logger::Id::tap> {
public:
  HttpPerRequestTapperImpl(HttpTapConfigImplSharedPtr config, uint64_t stream_id)
      : config_(std::move(config)), stream_id_(stream_id), statuses_(config_->numMatchers()) {
    config_->rootMatcher().onNewStream(statuses_);
  }

  // TapFilter::HttpPerRequestTapper
  void onRequestHeaders(const Http::HeaderMap& headers) override;
  void onRequestTrailers(const Http::HeaderMap& headers) override;
  void onResponseHeaders(const Http::HeaderMap& headers) override;
  void onResponseTrailers(const Http::HeaderMap& headers) override;
  bool onDestroyLog(const Http::HeaderMap* request_headers, const Http::HeaderMap* request_trailers,
                    const Http::HeaderMap* response_headers,
                    const Http::HeaderMap* response_trailers) override;

private:
  HttpTapConfigImplSharedPtr config_;
  const uint64_t stream_id_;
  std::vector<bool> statuses_;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
