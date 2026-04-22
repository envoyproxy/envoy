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
#include "source/extensions/filters/http/composite/action.h"

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

  // Bring base class overloads into scope to avoid hiding them.
  using DualFactoryBase::createFilterFactoryFromProto;

  // Override to compile named filter chains with FactoryContext access.
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message& config, const std::string& stats_prefix,
                               Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::composite::v3::Composite& proto_config,
      const std::string& stats_prefix, DualInfo dual_info,
      Server::Configuration::ServerFactoryContext& context) override;

  // Compiles named filter chains from the config.
  static absl::StatusOr<NamedFilterChainFactoryMapSharedPtr>
  compileNamedFilterChains(const envoy::extensions::filters::http::composite::v3::Composite& config,
                           const std::string& stats_prefix,
                           Server::Configuration::FactoryContext& context);
};

using UpstreamCompositeFilterFactory = CompositeFilterFactory;

DECLARE_FACTORY(CompositeFilterFactory);
DECLARE_FACTORY(UpstreamCompositeFilterFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
