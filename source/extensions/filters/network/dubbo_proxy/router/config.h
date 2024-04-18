#pragma once

#include "envoy/extensions/filters/network/dubbo_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/router/v3/router.pb.validate.h"

#include "source/extensions/filters/network/dubbo_proxy/filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

class RouterFilterConfig
    : public DubboFilters::FactoryBase<
          envoy::extensions::filters::network::dubbo_proxy::router::v3::Router> {
public:
  RouterFilterConfig() : FactoryBase("envoy.filters.dubbo.router") {}

private:
  DubboFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::dubbo_proxy::router::v3::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
