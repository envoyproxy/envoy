#pragma once

#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/common/factory_base.h"

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
  RouterFilterConfig() : FactoryBase("envoy.filters.http.router") {}

private:
  bool isTerminalFilterByProtoTyped(const envoy::extensions::filters::http::router::v3::Router&,
                                    Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::router::v3::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(RouterFilterConfig);

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
