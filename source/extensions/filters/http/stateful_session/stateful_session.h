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
#include "source/common/common/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

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
                        Server::Configuration::GenericFactoryContext& context,
                        const std::string& stats_prefix, Stats::Scope& scope);

  Http::SessionStatePtr createSessionState(Http::RequestHeaderMap& headers) const {
    ASSERT(factory_ != nullptr);
    return factory_->create(headers);
  }

  bool isStrict() const { return strict_; }

  OptRef<StatefulSessionFilterStats> stats() { return makeOptRefFromPtr(stats_.get()); }

private:
  Http::SessionStateFactorySharedPtr factory_;
  bool strict_{false};
  std::shared_ptr<StatefulSessionFilterStats> stats_;
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
  StatefulSession(StatefulSessionConfigSharedPtr config) : config_(std::move(config)) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

  Http::SessionStatePtr& sessionStateForTest() { return session_state_; }

private:
  void markRouted() {
    if (auto stats = effective_config_->stats(); stats.has_value()) {
      stats->routed_.inc();
    }
  }
  void markFailedClosed() {
    if (auto stats = effective_config_->stats(); stats.has_value()) {
      stats->failed_closed_.inc();
    }
  }
  void markFailedOpen() {
    if (auto stats = effective_config_->stats(); stats.has_value()) {
      stats->failed_open_.inc();
    }
  }

  Http::SessionStatePtr session_state_;
  StatefulSessionConfigSharedPtr config_;
  // Cached effective config resolved from route or base config.
  StatefulSessionConfig* effective_config_{nullptr};
};

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
