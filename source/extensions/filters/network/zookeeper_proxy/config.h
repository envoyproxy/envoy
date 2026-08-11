#pragma once

#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.h"
#include "envoy/extensions/filters/network/zookeeper_proxy/v3/zookeeper_proxy.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"
#include "source/extensions/filters/network/zookeeper_proxy/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * Config registration for the ZooKeeper proxy filter.
 */
class ZooKeeperConfigFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::network::zookeeper_proxy::v3::ZooKeeperProxy> {
public:
  ZooKeeperConfigFactory() : ExceptionFreeFactoryBase(NetworkFilterNames::get().ZooKeeperProxy) {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::zookeeper_proxy::v3::ZooKeeperProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
