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
    : public Common::UnifiedFactoryBase<envoy::extensions::filters::http::router::v3::Router> {
public:
  RouterFilterConfig() : UnifiedFactoryBase("envoy.filters.http.router") {}

private:
  bool isTerminalFilterByProtoTyped(const envoy::extensions::filters::http::router::v3::Router&,
                                    Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::router::v3::Router& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(RouterFilterConfig);

} // namespace RouterFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
