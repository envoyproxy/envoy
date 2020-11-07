#pragma once

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouterFilter {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class RouterFilterConfig
    : public Common::FactoryBase<envoy::extensions::filters::http::router::v3::Router> {
public:
  RouterFilterConfig() : FactoryBase(HttpFilterNames::get().Router) {}

  bool isTerminalFilter() override { return true; }

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::router::v3::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(RouterFilterConfig);

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
