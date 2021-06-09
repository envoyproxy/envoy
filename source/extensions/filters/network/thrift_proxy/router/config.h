#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/router/v3/router.pb.validate.h"

#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouterFilterConfig
    : public ThriftFilters::FactoryBase<
          envoy::extensions::filters::network::thrift_proxy::router::v3::Router> {
public:
  RouterFilterConfig() : FactoryBase("envoy.filters.thrift.router") {}

private:
  ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::router::v3::Router& proto_config,
      const std::string& stat_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
