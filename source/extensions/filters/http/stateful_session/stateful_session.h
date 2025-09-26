#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.pb.h"
#include "envoy/http/stateful_session.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

using ProtoConfig = envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using PerRouteProtoConfig =
    envoy::extensions::filters::http::stateful_session::v3::StatefulSessionPerRoute;

/**
 * All stats for the Stateful Session filter. @see stats_macros.h
 */
#define ALL_STATEFUL_SESSION_FILTER_STATS(COUNTER)                                                 \
  COUNTER(routed)                                                                                  \
  COUNTER(failed_open)                                                                             \
  COUNTER(failed_closed)

/**
 * Wrapper struct for Stateful Session filter stats. @see stats_macros.h
 */
struct StatefulSessionFilterStats {
  ALL_STATEFUL_SESSION_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

class StatefulSessionConfig {
public:
  StatefulSessionConfig(const ProtoConfig& config,
                        Server::Configuration::GenericFactoryContext& context);

  Http::SessionStatePtr createSessionState(Http::RequestHeaderMap& headers) const {
    ASSERT(factory_ != nullptr);
    return factory_->create(headers);
  }

  bool isStrict() const { return strict_; }

  const std::string& statPrefixOverride() const { return stat_prefix_override_; }

  void setStats(const std::shared_ptr<StatefulSessionFilterStats>& stats) { stats_ = stats; }
  const std::shared_ptr<StatefulSessionFilterStats>& stats() const { return stats_; }

private:
  Http::SessionStateFactorySharedPtr factory_;
  bool strict_{false};
  std::string stat_prefix_override_{};
  std::shared_ptr<StatefulSessionFilterStats> stats_{};
};
using StatefulSessionConfigSharedPtr = std::shared_ptr<StatefulSessionConfig>;

class PerRouteStatefulSession : public Router::RouteSpecificFilterConfig {
public:
  PerRouteStatefulSession(const PerRouteProtoConfig& config,
                          Server::Configuration::GenericFactoryContext& context);

  bool disabled() const { return disabled_; }
  StatefulSessionConfig* statefulSessionConfig() const { return config_.get(); }

private:
  bool disabled_{};
  StatefulSessionConfigSharedPtr config_;
};
using PerRouteStatefulSessionConfigSharedPtr = std::shared_ptr<PerRouteStatefulSession>;

class StatefulSession : public Http::PassThroughFilter,
                        public Logger::Loggable<Logger::Id::filter> {
public:
  StatefulSession(StatefulSessionConfigSharedPtr config,
                  std::shared_ptr<StatefulSessionFilterStats> stats)
      : config_(std::move(config)), stats_(std::move(stats)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

  // Http::StreamFilterBase
  Http::LocalErrorStatus onLocalReply(const Http::StreamFilterBase::LocalReplyData&) override {
    // For strict mode, if an override was attempted and a local reply was sent (e.g., 503),
    // consider this a failed-closed selection.
    if (override_attempted_ && config_->isStrict() && !accounted_) {
      markFailedClosed();
      accounted_ = true;
    }
    return Http::LocalErrorStatus::Continue;
  }

  Http::SessionStatePtr& sessionStateForTest() { return session_state_; }

private:
  void markOverrideAttempted() { override_attempted_ = true; }
  void markRouted() {
    if (override_attempted_ && stats_ != nullptr) {
      stats_->routed_.inc();
    }
  }
  void markFailedClosed() {
    if (override_attempted_ && stats_ != nullptr) {
      stats_->failed_closed_.inc();
    }
  }
  void markFailedOpen() {
    if (override_attempted_ && stats_ != nullptr) {
      stats_->failed_open_.inc();
    }
  }

  Http::SessionStatePtr session_state_;
  StatefulSessionConfigSharedPtr config_;
  std::shared_ptr<StatefulSessionFilterStats> stats_;
  bool override_attempted_{false};
  absl::optional<std::string> override_address_;
  bool accounted_{false};
};

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
