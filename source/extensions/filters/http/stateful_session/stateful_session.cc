#include "source/extensions/filters/http/stateful_session/stateful_session.h"

#include <cstdint>
#include <memory>

#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/upstream/load_balancer_context_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

namespace {

class EmptySessionStateFactory : public Envoy::Http::SessionStateFactory {
public:
  Envoy::Http::SessionStatePtr create(Envoy::Http::RequestHeaderMap&) const override {
    return nullptr;
  }
};

} // namespace

StatefulSessionConfig::StatefulSessionConfig(const ProtoConfig& config,
                                             Server::Configuration::GenericFactoryContext& context,
                                             const std::string& stats_prefix, Stats::Scope& scope)
    : strict_(config.strict()) {
  // Only construct stats if stat_prefix is explicitly set.
  if (!config.stat_prefix().empty()) {
    const std::string final_prefix =
        absl::StrCat(stats_prefix, "stateful_session.", config.stat_prefix(), ".");
    stats_ = std::make_shared<StatefulSessionFilterStats>(StatefulSessionFilterStats{
        ALL_STATEFUL_SESSION_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))});
  }

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
    const PerRouteProtoConfig& config, Server::Configuration::GenericFactoryContext& context) {
  if (config.override_case() == PerRouteProtoConfig::kDisabled) {
    disabled_ = true;
    return;
  }
  // Per-route configs never generate stats. Pass empty prefix to ensure no stats are created.
  config_ = std::make_shared<StatefulSessionConfig>(config.stateful_session(), context, "",
                                                    context.scope());
}

Http::FilterHeadersStatus StatefulSession::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  effective_config_ = config_.get();
  auto route_config = Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteStatefulSession>(
      decoder_callbacks_);

  if (route_config != nullptr) {
    if (route_config->disabled()) {
      return Http::FilterHeadersStatus::Continue;
    }
    effective_config_ = route_config->statefulSessionConfig();
  }
  session_state_ = effective_config_->createSessionState(headers);
  if (session_state_ == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (auto upstream_address = session_state_->upstreamAddress(); upstream_address.has_value()) {
    decoder_callbacks_->setUpstreamOverrideHost(
        std::make_pair(upstream_address.value(), effective_config_->isStrict()));
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
      const bool host_changed = session_state_->onUpdate(host->address()->asStringView(), headers);

      // Track stats based on whether the host changed.
      if (host_changed) {
        // Host changed means override failed and fallback occurred (non-strict mode).
        if (!effective_config_->isStrict()) {
          markFailedOpen();
        }
      } else {
        // Host didn't change, meaning the override was successful.
        markRouted();
      }
    }
  } else {
    // No upstream info. We should check for failed_closed or no healthy upstream
    // in the strict mode.
    const bool has_uh_flag = encoder_callbacks_->streamInfo().hasResponseFlag(
        StreamInfo::CoreResponseFlag::NoHealthyUpstream);

    if (has_uh_flag && effective_config_->isStrict()) {
      markFailedClosed();
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
