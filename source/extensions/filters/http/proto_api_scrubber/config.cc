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

FilterFactoryCreator::FilterFactoryCreator() : FactoryBase(kFilterName) {}

Envoy::Http::FilterFactoryCb FilterFactoryCreator::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
        proto_config,
    const std::string&, Envoy::Server::Configuration::FactoryContext& context) {
  absl::StatusOr<std::shared_ptr<ProtoApiScrubberFilterConfig>> filter_config =
      ProtoApiScrubberFilterConfig::create(proto_config, context);
  if (!filter_config.ok()) {
    // TODO: This should be allowed. No way to convert this to absl::Status. Check with Adi once.
    // throw Envoy::ProtoValidationException(std::string(filter_config.status().message()));
  }

  return [filter_config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<ProtoApiScrubberFilter>(*filter_config.value()));
  };
}

REGISTER_FACTORY(FilterFactoryCreator, Envoy::Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
