#include "source/extensions/filters/http/proto_api_scrubber/config.h"

#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/proto_api_scrubber/filter.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

FilterFactoryCreator::FilterFactoryCreator() : ExceptionFreeFactoryBase(kFilterName) {}

absl::StatusOr<Envoy::Http::FilterFactoryCb>
FilterFactoryCreator::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
        proto_config,
    const std::string&, Envoy::Server::Configuration::FactoryContext& context) {
  absl::StatusOr<std::shared_ptr<const ProtoApiScrubberFilterConfig>> filter_config_or_status =
      ProtoApiScrubberFilterConfig::create(proto_config, context);
  RETURN_IF_ERROR(filter_config_or_status.status());

  return [filter_config_or_status](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<ProtoApiScrubberFilter>(*filter_config_or_status.value()));
  };
}

REGISTER_FACTORY(FilterFactoryCreator, Envoy::Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
