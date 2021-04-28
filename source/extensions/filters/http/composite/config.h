#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "common/matcher/matcher.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

/**
 * Config registration for the composite filter. @see NamedHttpFilterConfigFactory.
 */
class CompositeFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::composite::v3::Composite> {
public:
  CompositeFilterFactory() : FactoryBase(HttpFilterNames::get().Composite) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::composite::v3::Composite& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Server::Configuration::MatchingRequirementsPtr matchingRequirements() override {
    auto requirements = std::make_unique<
        envoy::extensions::filters::common::dependency::v3::MatchingRequirements>();

    // This ensure that trees are only allowed to match on request headers, avoiding configurations
    // where the matcher requires data that will be available too late for the delegation to work
    // correctly.
    requirements->mutable_data_input_allow_list()->add_type_url(
        TypeUtil::descriptorFullNameToTypeUrl(
            envoy::type::matcher::v3::HttpRequestHeaderMatchInput::descriptor()->full_name()));

    return requirements;
  }
};

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
