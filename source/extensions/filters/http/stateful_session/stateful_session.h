#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.pb.h"
#include "envoy/http/stateful_session.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

using ProtoConfig = envoy::extensions::filters::http::stateful_session::v3::StatefulSession;
using PerRouteProtoConfig =
    envoy::extensions::filters::http::stateful_session::v3::StatefulSessionPerRoute;

class StatefulSessionConfig {
public:
  StatefulSessionConfig(const ProtoConfig& config,
                        Server::Configuration::CommonFactoryContext& context);

  Http::SessionStatePtr createSessionState(const Http::RequestHeaderMap& headers) const {
    ASSERT(factory_ != nullptr);
    return factory_->create(headers);
  }

private:
  Http::SessionStateFactorySharedPtr factory_;
};
using StatefulSessionConfigSharedPtr = std::shared_ptr<StatefulSessionConfig>;

class PerRouteStatefulSession : public Router::RouteSpecificFilterConfig {
public:
  PerRouteStatefulSession(const PerRouteProtoConfig& config,
                          Server::Configuration::CommonFactoryContext& context);

  bool disabled() const { return disabled_; }
  StatefulSessionConfig* statefuleSessionConfig() const { return config_.get(); }

private:
  bool disabled_{};
  StatefulSessionConfigSharedPtr config_;
};
using PerRouteStatefulSessionConfigSharedPtr = std::shared_ptr<PerRouteStatefulSession>;

class StatefulSession : public Http::PassThroughFilter,
                        public Logger::Loggable<Logger::Id::filter> {
public:
  StatefulSession(const StatefulSessionConfig* config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

  Http::SessionStatePtr& sessionStateForTest() { return session_state_; }

private:
  Http::SessionStatePtr session_state_;

  const StatefulSessionConfig* config_{nullptr};
};

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
