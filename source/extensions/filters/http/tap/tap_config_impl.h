#pragma once

#include "envoy/http/header_map.h"

#include "common/common/logger.h"
#include "common/http/header_utility.h"

#include "extensions/common/tap/tap_config_base.h"
#include "extensions/filters/http/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

class HttpTapConfigImpl : Extensions::Common::Tap::TapConfigBaseImpl,
                          public HttpTapConfig,
                          public std::enable_shared_from_this<HttpTapConfigImpl> {
public:
  struct RequestMatchConfig {
    std::vector<Http::HeaderUtility::HeaderData> headers_to_match_;
  };

  struct ResponseMatchConfig {
    std::vector<Http::HeaderUtility::HeaderData> headers_to_match_;
  };

  struct MatchConfig {
    std::string id_;
    absl::optional<RequestMatchConfig> request_config_;
    absl::optional<ResponseMatchConfig> response_config_;
  };

  struct MatchStatus {
    bool request_matched_{};
    bool response_matched_{};
  };

  HttpTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                    Extensions::Common::Tap::Sink* admin_streamer);

  const std::vector<MatchConfig>& configs() { return match_configs_; }
  void matchesRequestHeaders(const Http::HeaderMap& headers, std::vector<MatchStatus>& statuses);
  void matchesResponseHeaders(const Http::HeaderMap& headers, std::vector<MatchStatus>& statuses);
  Extensions::Common::Tap::Sink& sink() {
    // TODO(mattklein123): When we support multiple sinks, select the right one. Right now
    // it must be admin.
    return *admin_streamer_;
  }

  // TapFilter::HttpTapConfig
  HttpPerRequestTapperPtr createPerRequestTapper() override;

private:
  Extensions::Common::Tap::Sink* admin_streamer_;
  std::vector<MatchConfig> match_configs_;
};

using HttpTapConfigImplSharedPtr = std::shared_ptr<HttpTapConfigImpl>;

class HttpPerRequestTapperImpl : public HttpPerRequestTapper, Logger::Loggable<Logger::Id::tap> {
public:
  HttpPerRequestTapperImpl(HttpTapConfigImplSharedPtr config)
      : config_(std::move(config)), statuses_(config_->configs().size()) {}

  // TapFilter::HttpPerRequestTapper
  void onRequestHeaders(const Http::HeaderMap& headers) override;
  void onResponseHeaders(const Http::HeaderMap& headers) override;
  bool onDestroyLog(const Http::HeaderMap* request_headers,
                    const Http::HeaderMap* response_headers) override;

private:
  HttpTapConfigImplSharedPtr config_;
  std::vector<HttpTapConfigImpl::MatchStatus> statuses_;
};

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
