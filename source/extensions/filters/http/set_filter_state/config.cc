#include "source/extensions/filters/http/set_filter_state/config.h"

#include <string>

#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.validate.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/registry/registry.h"

#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetFilterState {

using envoy::extensions::filters::http::set_filter_state::v3::Config;

SetFilterState::SetFilterState(const Filters::Common::SetFilterState::ConfigSharedPtr config)
    : config_(config) {}

Http::FilterHeadersStatus SetFilterState::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Apply listener level configuration first.
  config_.get()->updateFilterState({&headers}, decoder_callbacks_->streamInfo());

  // If configured, apply virtual host and then route level configuration next.
  auto policies = Http::Utility::getAllPerFilterConfig<Filters::Common::SetFilterState::Config>(
      decoder_callbacks_);
  for (auto policy : policies) {
    policy.get().updateFilterState({&headers}, decoder_callbacks_->streamInfo());
  }
  if (config_->clearRouteCache() && decoder_callbacks_->downstreamCallbacks()) {
    decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
  }
  return Http::FilterHeadersStatus::Continue;
}

absl::StatusOr<StreamInfo::FilterState::LifeSpan>
SetFilterStateConfig::toFilterStateLifeSpan(Config::LifeSpan life_span) {
  switch (life_span) {
  case Config::DEFAULT:
  case Config::FILTER_CHAIN:
    return StreamInfo::FilterState::LifeSpan::FilterChain;
  case Config::REQUEST:
    return StreamInfo::FilterState::LifeSpan::Request;
  case Config::CONNECTION:
    return StreamInfo::FilterState::LifeSpan::Connection;
  default:
    return absl::InvalidArgumentError("Invalid LifeSpan");
  }
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
SetFilterStateConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::set_filter_state::v3::Config& proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {

  Server::GenericFactoryContextImpl generic_context(context, context.messageValidationVisitor());
  auto life_span_or_error = toFilterStateLifeSpan(proto_config.life_span());
  RETURN_IF_NOT_OK_REF(life_span_or_error.status());

  return std::make_shared<const Filters::Common::SetFilterState::Config>(
      proto_config.on_request_headers(), *life_span_or_error, generic_context,
      proto_config.clear_route_cache());
}

absl::StatusOr<Http::FilterFactoryCb> SetFilterStateConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::set_filter_state::v3::Config& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    Server::Configuration::ExtraFactoryContext& extra_context) {

  Server::GenericFactoryContextImpl generic_context(
      context, extra_context.scope, extra_context.visitor, extra_context.init_manager);
  auto life_span_or_error = toFilterStateLifeSpan(proto_config.life_span());
  RETURN_IF_NOT_OK_REF(life_span_or_error.status());

  const auto filter_config = std::make_shared<Filters::Common::SetFilterState::Config>(
      proto_config.on_request_headers(), *life_span_or_error, generic_context,
      proto_config.clear_route_cache());
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new SetFilterState(filter_config)});
  };
}

REGISTER_FACTORY(SetFilterStateConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace SetFilterState
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
