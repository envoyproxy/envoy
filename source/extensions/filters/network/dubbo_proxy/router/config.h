#pragma once

#include "envoy/config/filter/dubbo/router/v2alpha1/router.pb.h"
#include "envoy/config/filter/dubbo/router/v2alpha1/router.pb.validate.h"

#include "extensions/filters/network/dubbo_proxy/filters/factory_base.h"
#include "extensions/filters/network/dubbo_proxy/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

class RouterFilterConfig
    : public DubboFilters::FactoryBase<envoy::config::filter::dubbo::router::v2alpha1::Router> {
public:
  RouterFilterConfig() : FactoryBase(DubboFilters::DubboFilterNames::get().ROUTER) {}

private:
  DubboFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::dubbo::router::v2alpha1::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
