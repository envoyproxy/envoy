#include "extensions/filters/http/experimental_compiler_features/config.h"

#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.h"
#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/experimental_compiler_features/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExperimentalCompilerFeatures {

Http::FilterFactoryCb ExperimentalCompilerFeaturesFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::experimental_compiler_features::v3::
        ExperimentalCompilerFeatures& config,
    const std::string&, Server::Configuration::FactoryContext&) {

  auto filter_config = std::make_shared<FilterConfigImpl>(
      config.key(), config.value(), config.associative_container_use_contains(),
      config.enum_members_in_scope(), config.str_starts_with(), config.str_ends_with(),
      config.enum_value(), config.start_end_string(), config.associative_container_string());

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(filter_config);
    callbacks.addStreamDecoderFilter(filter);
  };
}

/**
 * Static registration for the Experimental Compiler Features filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ExperimentalCompilerFeaturesFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ExperimentalCompilerFeatures
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
