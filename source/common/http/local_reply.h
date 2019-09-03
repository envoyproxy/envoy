#pragma once

#include <list>
#include <string>
#include <vector>

#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"
#include "envoy/config/filter/accesslog/v2/accesslog.pb.validate.h"

#include "common/common/matchers.h"

namespace Envoy {
namespace Http {
  static uint64_t toBitMask(const envoy::data::accesslog::v2::ResponseFlags& response_flags){
    uint64_t bit_mask = 0;
    if (response_flags.failed_local_healthcheck()) {
      bit_mask |= StreamInfo::ResponseFlag::FailedLocalHealthCheck;
    }

    if (response_flags.no_healthy_upstream()) {
      bit_mask |= StreamInfo::ResponseFlag::NoHealthyUpstream;
    }

    if (response_flags.upstream_request_timeout()) {
      bit_mask |= StreamInfo::ResponseFlag::UpstreamRequestTimeout;
    }

    if (response_flags.local_reset()) {
      bit_mask |= StreamInfo::ResponseFlag::LocalReset;
    }

    if (response_flags.upstream_remote_reset()) {
      bit_mask |= StreamInfo::ResponseFlag::UpstreamRemoteReset;
    }

    if (response_flags.upstream_connection_failure()) {
      bit_mask |= StreamInfo::ResponseFlag::UpstreamConnectionFailure;
    }

    if (response_flags.upstream_connection_termination()) {
      bit_mask |= StreamInfo::ResponseFlag::UpstreamConnectionTermination;
    }

    if (response_flags.upstream_overflow()) {
      bit_mask |= StreamInfo::ResponseFlag::UpstreamOverflow;
    }

    if (response_flags.no_route_found()) {
      bit_mask |= StreamInfo::ResponseFlag::NoRouteFound;
    }

    if (response_flags.delay_injected()) {
      bit_mask |= StreamInfo::ResponseFlag::DelayInjected;
    }

    if (response_flags.fault_injected()) {
      bit_mask |= StreamInfo::ResponseFlag::FaultInjected;
    }

    if (response_flags.rate_limited()) {
      bit_mask |= StreamInfo::ResponseFlag::RateLimited;
    }

    // if (response_flags.unauthorized_details()) {
    //   bit_mask |= StreamInfo::ResponseFlag::UnauthorizedExternalService;
    // }

    if (response_flags.rate_limit_service_error()) {
      bit_mask |= StreamInfo::ResponseFlag::RateLimitServiceError;
    }

    if (response_flags.downstream_connection_termination()) {
      bit_mask |= StreamInfo::ResponseFlag::DownstreamConnectionTermination;
    }

    if (response_flags.upstream_retry_limit_exceeded()) {
      bit_mask |= StreamInfo::ResponseFlag::UpstreamRetryLimitExceeded;
    }

    if (response_flags.stream_idle_timeout()) {
      bit_mask |= StreamInfo::ResponseFlag::StreamIdleTimeout;
    }

    if (response_flags.invalid_envoy_request_headers()) {
      bit_mask |= StreamInfo::ResponseFlag::InvalidEnvoyRequestHeaders;
    }

    return bit_mask;
  }

struct LocalReplyMatcher {
  LocalReplyMatcher(std::vector<uint32_t>& status_codes,
                    const envoy::type::matcher::StringMatcher& body_pattern,
                    const envoy::data::accesslog::v2::ResponseFlags& response_flags)
      : status_codes_(move(status_codes)), body_pattern_(body_pattern), response_flags_(toBitMask(response_flags)){};

  bool match(const absl::string_view value) const { 
    return body_pattern_.match(value); 
  }

  std::vector<uint32_t> status_codes_;
  Matchers::StringMatcherImpl body_pattern_;
  uint64_t response_flags_;
};

/**
 * Structure which holds rewriter configuration from proto file for LocalReplyConfig.
 */
struct LocalReplyRewriter {
  uint32_t status_;
};

class LocalReplyConfig {
public:
  LocalReplyConfig(
      const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
          config) {
    for (const auto& match_rewrite_config : config.send_local_reply_config()) {
      std::vector<uint32_t> status_codes;
      for (const auto code : match_rewrite_config.match().status_codes()) {
        status_codes.emplace_back(code);
      }

      std::pair<LocalReplyMatcher, LocalReplyRewriter> pair = std::make_pair(
          LocalReplyMatcher{status_codes, match_rewrite_config.match().body_pattern(), match_rewrite_config.match().response_flags()},
          LocalReplyRewriter{match_rewrite_config.rewrite().status()});
      match_rewrite_pair_list_.emplace_back(std::move(pair));
    }
  };
private:
  std::list<std::pair<LocalReplyMatcher, LocalReplyRewriter>> match_rewrite_pair_list_;
};

using LocalReplyConfigConstPtr = std::unique_ptr<const LocalReplyConfig>;

} // namespace Http
} // namespace Envoy
