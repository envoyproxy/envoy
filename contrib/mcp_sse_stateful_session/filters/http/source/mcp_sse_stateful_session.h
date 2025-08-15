#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/http/mcp_sse_stateful_session.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/server/generic_factory_context.h"

#include "absl/strings/string_view.h"
#include "contrib/envoy/extensions/filters/http/mcp_sse_stateful_session/v3alpha/mcp_sse_stateful_session.pb.h"
#include "contrib/envoy/extensions/filters/http/mcp_sse_stateful_session/v3alpha/mcp_sse_stateful_session.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpSseStatefulSession {

using ProtoConfig =
    envoy::extensions::filters::http::mcp_sse_stateful_session::v3alpha::McpSseStatefulSession;
using PerRouteProtoConfig = envoy::extensions::filters::http::mcp_sse_stateful_session::v3alpha::
    McpSseStatefulSessionPerRoute;

class McpSseStatefulSessionConfig {
public:
  McpSseStatefulSessionConfig(const ProtoConfig& config,
                              Server::Configuration::GenericFactoryContext& context);

  Envoy::Http::McpSseSessionStatePtr
  createSessionState(Envoy::Http::RequestHeaderMap& headers) const {
    ASSERT(factory_ != nullptr);
    return factory_->create(headers);
  }

  bool isStrict() const { return strict_; }

private:
  Envoy::Http::McpSseSessionStateFactorySharedPtr factory_;
  bool strict_{false};
};
using McpSseStatefulSessionConfigSharedPtr = std::shared_ptr<McpSseStatefulSessionConfig>;

class PerRouteMcpSseStatefulSession : public Router::RouteSpecificFilterConfig {
public:
  PerRouteMcpSseStatefulSession(const PerRouteProtoConfig& config,
                                Server::Configuration::GenericFactoryContext& context);

  bool disabled() const { return disabled_; }
  McpSseStatefulSessionConfig* statefulSessionConfig() const { return config_.get(); }

private:
  bool disabled_{};
  McpSseStatefulSessionConfigSharedPtr config_;
};
using PerRouteMcpSseStatefulSessionConfigSharedPtr = std::shared_ptr<PerRouteMcpSseStatefulSession>;

class McpSseStatefulSession : public Envoy::Http::PassThroughFilter,
                              public Logger::Loggable<Logger::Id::filter> {
public:
  McpSseStatefulSession(McpSseStatefulSessionConfigSharedPtr config) : config_(std::move(config)) {}

  // Http::StreamDecoderFilter
  Envoy::Http::FilterHeadersStatus decodeHeaders(Envoy::Http::RequestHeaderMap& headers,
                                                 bool) override;

  // Http::StreamEncoderFilter
  Envoy::Http::FilterHeadersStatus encodeHeaders(Envoy::Http::ResponseHeaderMap& headers,
                                                 bool) override;

  Envoy::Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  Envoy::Http::McpSseSessionStatePtr& sessionStateForTest() { return session_state_; }

private:
  Envoy::Http::McpSseSessionStatePtr session_state_;
  McpSseStatefulSessionConfigSharedPtr config_;
};

} // namespace McpSseStatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
