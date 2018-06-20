#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/v2alpha1/router/router.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v2alpha1/router/router.pb.validate.h"

#include "extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "extensions/filters/network/thrift_proxy/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouterFilterConfig
    : public ThriftFilters::FactoryBase<
          envoy::extensions::filters::network::thrift_proxy::v2alpha1::router::Router> {
public:
  RouterFilterConfig() : FactoryBase(ThriftFilters::ThriftFilterNames::get().ROUTER) {}

private:
  ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::v2alpha1::router::Router&
          proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
