#include "server/config/stats/udp_statsd.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/network/address_impl.h"
#include "common/stats/statsd.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr UdpStatsdSinkFactory::createStatsSink(const Json::Object& json_config,
                                                     Server::Instance& server,
                                                     Upstream::ClusterManager& cluster_manager) {
  UNREFERENCED_PARAMETER(cluster_manager);
  if (json_config.hasObject("local_port") && json_config.hasObject("ip_address")) {
    throw EnvoyException(fmt::format(
        "local_port and ip_address are mutually exclusive in the {} Stats::Sink config", name()));
  }

  if (json_config.hasObject("ip_address")) {
    const std::string udp_ip_address = json_config.getString("ip_address");
    ENVOY_LOG(info, "statsd UDP ip address: {}", udp_ip_address);
    return Stats::SinkPtr(new Stats::Statsd::UdpStatsdSink(
        server.threadLocal(), Network::Utility::parseInternetAddressAndPort(udp_ip_address)));
  } else if (json_config.hasObject("local_port")) {
    const int64_t local_upd_port = json_config.getInteger("local_port");
    // TODO(hennna): DEPRECATED - statsdUdpPort will be removed in 1.4.0.
    ENVOY_LOG(warn, "local_port has been DEPRECATED and will be removed in 1.4.0. "
                    "Consider setting ip_address instead.");
    ENVOY_LOG(info, "statsd UDP port: {}", local_upd_port);
    Network::Address::InstanceConstSharedPtr address(
        new Network::Address::Ipv4Instance(local_upd_port));
    return Stats::SinkPtr(
        new Stats::Statsd::UdpStatsdSink(server.threadLocal(), std::move(address)));
  }

  throw EnvoyException(
      fmt::format("Didn't find local_port or ip_address in the {} Stats::Sink config", name()));
}

std::string UdpStatsdSinkFactory::name() { return "statsd_udp"; }

/**
 * Static registration for the udp statsd sink. @see RegisterFactory.
 */
static Registry::RegisterFactory<UdpStatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
