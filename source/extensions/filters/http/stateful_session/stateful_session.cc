#include "source/extensions/filters/http/stateful_session/stateful_session.h"

#include <cstdint>
#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

namespace {

class EmptySessionStateFactory : public Envoy::Http::SessionStateFactory {
public:
  Envoy::Http::SessionStatePtr create(const Envoy::Http::RequestHeaderMap&) const override {
    return nullptr;
  }
};

} // namespace

StatefulSessionConfig::StatefulSessionConfig(const ProtoConfig& config,
                                             Server::Configuration::CommonFactoryContext& context) {
  if (!config.has_session_state()) {
    factory_ = std::make_shared<EmptySessionStateFactory>();
    return;
  }

  auto& factory =
      Envoy::Config::Utility::getAndCheckFactoryByName<Envoy::Http::SessionStateFactoryConfig>(
          config.session_state().name());

  auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.session_state().typed_config(), context.messageValidationVisitor(), factory);

  factory_ = factory.createSessionStateFactory(*typed_config, context);
}

PerRouteStatefulSession::PerRouteStatefulSession(
    const PerRouteProtoConfig& config, Server::Configuration::CommonFactoryContext& context) {
  if (config.override_case() == PerRouteProtoConfig::kDisabled) {
    disabled_ = true;
    return;
  }
  config_ = std::make_shared<StatefulSessionConfig>(config.stateful_session(), context);
}

Http::FilterHeadersStatus StatefulSession::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const StatefulSessionConfig* config = config_.get();
  auto route_config = Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteStatefulSession>(
      decoder_callbacks_);

  if (route_config != nullptr) {
    if (route_config->disabled()) {
      return Http::FilterHeadersStatus::Continue;
    }
    config = route_config->statefuleSessionConfig();
  }
  session_state_ = config->createSessionState(headers);
  if (session_state_ == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (auto upstream_address = session_state_->upstreamAddress(); upstream_address.has_value()) {
    decoder_callbacks_->setUpstreamOverrideHost(upstream_address.value());
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus StatefulSession::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (session_state_ == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (auto upstream_info = encoder_callbacks_->streamInfo().upstreamInfo();
      upstream_info != nullptr) {
    auto host = upstream_info->upstreamHost();
    if (host != nullptr) {
      session_state_->onUpdate(*host, headers);
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
