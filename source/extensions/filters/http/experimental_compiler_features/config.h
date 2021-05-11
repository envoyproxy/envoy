#pragma once

#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.h"
#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExperimentalCompilerFeatures {

/**
 * Config registration for the Experimental Compiler Features filter.
 */
class ExperimentalCompilerFeaturesFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::experimental_compiler_features::
                                     v3::ExperimentalCompilerFeatures> {
public:
  ExperimentalCompilerFeaturesFactory()
      : FactoryBase(HttpFilterNames::get().ExperimentalCompilerFeatures) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::experimental_compiler_features::v3::
          ExperimentalCompilerFeatures& proto_config,
      const std::string&, Server::Configuration::FactoryContext&) override;
};

} // namespace ExperimentalCompilerFeatures
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
