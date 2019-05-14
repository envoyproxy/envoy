#pragma once

#include "envoy/config/filter/network/zookeeper_proxy/v1alpha1/zookeeper_proxy.pb.h"
#include "envoy/config/filter/network/zookeeper_proxy/v1alpha1/zookeeper_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"
#include "extensions/filters/network/zookeeper_proxy/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * Config registration for the ZooKeeper proxy filter.
 */
class ZooKeeperConfigFactory
    : public Common::FactoryBase<
          envoy::config::filter::network::zookeeper_proxy::v1alpha1::ZooKeeperProxy> {
public:
  ZooKeeperConfigFactory() : FactoryBase(NetworkFilterNames::get().ZooKeeperProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::zookeeper_proxy::v1alpha1::ZooKeeperProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
