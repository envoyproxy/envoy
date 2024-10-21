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
  // TODO: instead of loading modules here, we load the modules at the startup of Envoy.
  const auto dynamic_module =
      Extensions::DynamicModules::newDynamicModuleByName(module_config.name(), true);
  if (!dynamic_module.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module.status().message()));
  }
  auto filter_config = std::make_shared<
      Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilterConfig>(
      proto_config.filter_name(), proto_config.filter_config(), dynamic_module.value());

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter =
        std::make_shared<Envoy::Extensions::DynamicModules::HttpFilters::DynamicModuleHttpFilter>(
            filter_config);
    callbacks.addStreamDecoderFilter(filter);
    callbacks.addStreamEncoderFilter(filter);
  };
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
