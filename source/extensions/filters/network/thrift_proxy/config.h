#pragma once

#include <string>

#include "envoy/extensions/filters/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v2alpha1/thrift_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Config registration for the thrift proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class ThriftProxyFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::thrift_proxy::v2alpha1::ThriftProxy> {
public:
  ThriftProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().THRIFT_PROXY) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::v2alpha1::ThriftProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
