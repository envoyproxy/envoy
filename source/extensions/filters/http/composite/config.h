#pragma once

#include "envoy/extensions/filters/http/composite/v3/composite.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

/**
 * Config registration for the composite filter. @see NamedHttpFilterConfigFactory.
 */
class CompositeFilterFactory
    : public Common::DualFactoryBase<envoy::extensions::filters::http::composite::v3::Composite> {
public:
  CompositeFilterFactory() : DualFactoryBase("envoy.filters.http.composite") {}

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::composite::v3::Composite& proto_config,
      const std::string& stats_prefix, DualInfo dual_info,
      Server::Configuration::ServerFactoryContext& context) override;

  Server::Configuration::MatchingRequirementsPtr matchingRequirements() override {
    auto requirements = std::make_unique<
        envoy::extensions::filters::common::dependency::v3::MatchingRequirements>();

    // This ensure that trees are only allowed to match on request headers, avoiding configurations
    // where the matcher requires data that will be available too late for the delegation to work
    // correctly.
    auto* allow_list = requirements->mutable_data_input_allow_list();
    allow_list->add_type_url(TypeUtil::descriptorFullNameToTypeUrl(
        envoy::type::matcher::v3::HttpRequestHeaderMatchInput::descriptor()->full_name()));
    // CEL matcher and its input is also allowed.
    allow_list->add_type_url(TypeUtil::descriptorFullNameToTypeUrl(
        xds::type::matcher::v3::HttpAttributesCelMatchInput::descriptor()->full_name()));

    return requirements;
  }
};

using UpstreamCompositeFilterFactory = CompositeFilterFactory;

DECLARE_FACTORY(CompositeFilterFactory);
DECLARE_FACTORY(UpstreamCompositeFilterFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
