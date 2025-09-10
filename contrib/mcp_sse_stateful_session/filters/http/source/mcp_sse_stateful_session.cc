#include "contrib/mcp_sse_stateful_session/filters/http/source/mcp_sse_stateful_session.h"

#include <cstdint>
#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/upstream/load_balancer_context_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpSseStatefulSession {

namespace {

class EmptySessionStateFactory : public Envoy::Http::SseSessionStateFactory {
public:
  Envoy::Http::SseSessionStatePtr create(Envoy::Http::RequestHeaderMap&) const override {
    return nullptr;
  }
};

} // namespace

McpSseStatefulSessionConfig::McpSseStatefulSessionConfig(
    const ProtoConfig& config, Server::Configuration::GenericFactoryContext& context)
    : strict_(config.strict()) {
  if (!config.has_sse_session_state()) {
    factory_ = std::make_shared<EmptySessionStateFactory>();
    return;
  }

  auto& factory =
      Envoy::Config::Utility::getAndCheckFactoryByName<Envoy::Http::SseSessionStateFactoryConfig>(
          config.sse_session_state().name());

  auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.sse_session_state().typed_config(), context.messageValidationVisitor(), factory);

  factory_ = factory.createSseSessionStateFactory(*typed_config, context);
}

PerRouteMcpSseStatefulSession::PerRouteMcpSseStatefulSession(
    const PerRouteProtoConfig& config, Server::Configuration::GenericFactoryContext& context) {
  if (config.override_case() == PerRouteProtoConfig::kDisabled) {
    disabled_ = true;
    return;
  }
  config_ =
      std::make_shared<McpSseStatefulSessionConfig>(config.mcp_sse_stateful_session(), context);
}

Envoy::Http::FilterHeadersStatus
McpSseStatefulSession::decodeHeaders(Envoy::Http::RequestHeaderMap& headers, bool) {
  const auto route_config =
      Envoy::Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteMcpSseStatefulSession>(
          decoder_callbacks_);

  if (route_config != nullptr && route_config->disabled()) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  const McpSseStatefulSessionConfig& effective_config =
      (route_config != nullptr) ? *route_config->statefulSessionConfig() : *config_;

  session_state_ = effective_config.createSessionState(headers);
  if (session_state_ == nullptr) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  if (auto upstream_address = session_state_->upstreamAddress(); upstream_address.has_value()) {
    decoder_callbacks_->setUpstreamOverrideHost(
        std::make_pair(upstream_address.value(), effective_config.isStrict()));
  }
  return Envoy::Http::FilterHeadersStatus::Continue;
}

Envoy::Http::FilterHeadersStatus
McpSseStatefulSession::encodeHeaders(Envoy::Http::ResponseHeaderMap& headers, bool) {
  if (session_state_ == nullptr) {
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  if (auto upstream_info = encoder_callbacks_->streamInfo().upstreamInfo();
      upstream_info != nullptr) {
    auto host = upstream_info->upstreamHost();
    if (host != nullptr) {
      session_state_->onUpdateHeader(host->address()->asStringView(), headers);
    }
  }

  return Envoy::Http::FilterHeadersStatus::Continue;
}

Envoy::Http::FilterDataStatus McpSseStatefulSession::encodeData(Buffer::Instance& data,
                                                                bool end_stream) {
  if (session_state_ == nullptr) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  if (auto upstream_info = encoder_callbacks_->streamInfo().upstreamInfo();
      upstream_info != nullptr) {
    auto host = upstream_info->upstreamHost();
    if (host != nullptr) {
      return session_state_->onUpdateData(host->address()->asStringView(), data, end_stream);
    }
  }
  return Envoy::Http::FilterDataStatus::Continue;
}

} // namespace McpSseStatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
