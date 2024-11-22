#include "source/extensions/filters/http/dynamic_modules/factory.h"

#include "source/extensions/filters/http/dynamic_modules/filter.h"
#include "source/extensions/filters/http/dynamic_modules/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

absl::StatusOr<Http::FilterFactoryCb> DynamicModuleConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& raw_config, const std::string&, FactoryContext& context) {

  const auto proto_config = Envoy::MessageUtil::downcastAndValidate<const FilterConfig&>(
      raw_config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close());
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }

  absl::StatusOr<
      Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfigSharedPtr>
      filter_config =
          Envoy::Extensions::DynamicModules::HttpFilters::newDynamicModuleHttpFilterConfig(
              proto_config.filter_name(), proto_config.filter_config(),
              std::move(dynamic_module.value()));

  if (!filter_config.ok()) {
    return absl::InvalidArgumentError("Failed to create filter config: " +
                                      std::string(filter_config.status().message()));
  }
  return [config = filter_config.value()](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter =
        std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
            config);
    filter->initializeInModuleFilter();
    callbacks.addStreamDecoderFilter(filter);
    callbacks.addStreamEncoderFilter(filter);
  };
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
