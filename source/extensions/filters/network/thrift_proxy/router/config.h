#pragma once

#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.h"
#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.validate.h"

#include "extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "extensions/filters/network/thrift_proxy/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouterFilterConfig
    : public ThriftFilters::FactoryBase<envoy::config::filter::thrift::router::v2alpha1::Router> {
public:
  RouterFilterConfig() : FactoryBase(ThriftFilters::ThriftFilterNames::get().ROUTER) {}

private:
  ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::thrift::router::v2alpha1::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
