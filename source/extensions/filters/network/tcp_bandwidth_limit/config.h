#pragma once

#include "envoy/extensions/filters/network/tcp_bandwidth_limit/v3/tcp_bandwidth_limit.pb.h"
#include "envoy/extensions/filters/network/tcp_bandwidth_limit/v3/tcp_bandwidth_limit.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {

class TcpBandwidthLimitConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit> {
public:
  TcpBandwidthLimitConfigFactory() : FactoryBase("envoy.filters.network.tcp_bandwidth_limit") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit&
          proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
