#include "source/extensions/filters/http/set_filter_state/config.h"

#include <string>

#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/http/set_filter_state/v3/set_filter_state.pb.validate.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/server/generic_factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetFilterState {

SetFilterState::SetFilterState(const Filters::Common::SetFilterState::ConfigSharedPtr config)
    : config_(config) {}

Http::FilterHeadersStatus SetFilterState::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  config_->updateFilterState({&headers}, decoder_callbacks_->streamInfo());
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterFactoryCb SetFilterStateConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::set_filter_state::v3::Config& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  Server::GenericFactoryContextImpl generic_context(context);

  const auto filter_config = std::make_shared<Filters::Common::SetFilterState::Config>(
      proto_config.on_request_headers(), StreamInfo::FilterState::LifeSpan::FilterChain,
      generic_context);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new SetFilterState(filter_config)});
  };
}

Http::FilterFactoryCb SetFilterStateConfig::createFilterFactoryFromProtoWithServerContextTyped(
    const envoy::extensions::filters::http::set_filter_state::v3::Config& proto_config,
    const std::string&, Server::Configuration::ServerFactoryContext& context) {

  // TODO(wbpcode): these is a potential bug of message validation. The validation visitor
  // of server context should not be used here directly. But this is bug of
  // 'createFilterFactoryFromProtoWithServerContext' and will be fixed in the future.
  Server::GenericFactoryContextImpl generic_context(context, context.messageValidationVisitor());

  const auto filter_config = std::make_shared<Filters::Common::SetFilterState::Config>(
      proto_config.on_request_headers(), StreamInfo::FilterState::LifeSpan::FilterChain,
      generic_context);
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
