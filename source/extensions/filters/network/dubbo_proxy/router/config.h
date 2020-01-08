#pragma once

#include "envoy/extensions/filters/network/dubbo_proxy/router/v3alpha/router.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/router/v3alpha/router.pb.validate.h"

#include "extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "extensions/filters/network/dubbo_proxy/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

class RouterFilterConfig
    : public DubboFilters::FactoryBase<
          envoy::extensions::filters::network::dubbo_proxy::router::v3alpha::Router> {
public:
  RouterFilterConfig() : FactoryBase(DubboFilters::DubboFilterNames::get().ROUTER) {}

private:
  DubboFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::dubbo_proxy::router::v3alpha::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
