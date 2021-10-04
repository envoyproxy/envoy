#include "source/extensions/filters/http/stateful_session/stateful_session.h"

#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

StatefulSessionConfig::StatefulSessionConfig(const ProtoConfig& config,
                                             Server::Configuration::CommonFactoryContext& context) {
  std::vector<envoy::config::core::v3::HealthStatus> host_statuses;
  for (int i = 0; i < config.host_statuses_size(); i++) {
    host_statuses.push_back(config.host_statuses(i));
  }
  host_statuses_ = Upstream::LoadBalancerContextBase::createOverrideHostStatus(host_statuses);

  auto& factory =
      Envoy::Config::Utility::getAndCheckFactoryByName<Envoy::Http::SessionStateFactoryConfig>(
          config.session_state().name());

  factory_ = factory.createSessionStateFactory(config.session_state().typed_config(), context);
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
  const StatefulSessionConfig* config = config_;
  auto route_config = Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteStatefulSession>(
      "envoy.filters.http.stateful_session", decoder_callbacks_->route());

  if (route_config != nullptr) {
    if (route_config->disabled()) {
      return Http::FilterHeadersStatus::Continue;
    }
    config = route_config->statefuleSessionConfig();
  }
  session_state_ = config->createSessionState(headers);

  auto upstream_address = session_state_->upstreamAddress();
  if (upstream_address.has_value()) {
    decoder_callbacks_->setUpstreamOverrideHost(
        std::make_pair(std::string(upstream_address.value()), config->expectedHostStatus()));
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus StatefulSession::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (session_state_ == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  auto host = encoder_callbacks_->streamInfo().upstreamHost();
  if (host == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }
  session_state_->onUpdate(*host, headers);
  return Http::FilterHeadersStatus::Continue;
}

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
