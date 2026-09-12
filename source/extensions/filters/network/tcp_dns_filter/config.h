#pragma once

#include "envoy/extensions/filters/tcp/dns_filter/v3/dns_filter.pb.h"
#include "envoy/extensions/filters/tcp/dns_filter/v3/dns_filter.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/tcp_dns_filter/tcp_dns_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpDnsFilter {

/**
 * Config registration for the TCP DNS filter. @see NamedNetworkFilterConfigFactory.
 */
class TcpDnsFilterConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::tcp::dns_filter::v3::DnsFilterConfig> {
public:
  TcpDnsFilterConfigFactory() : FactoryBase("envoy.filters.network.tcp_dns_filter") {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::tcp::dns_filter::v3::DnsFilterConfig& config,
      Server::Configuration::FactoryContext& context) override;

  bool isTerminalFilterByProtoTyped(
      const envoy::extensions::filters::tcp::dns_filter::v3::DnsFilterConfig&,
      Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

} // namespace TcpDnsFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
