#include "server/config/stats/tcp_statsd.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/stats/statsd.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr TcpStatsdSinkFactory::createStatsSink(const Json::Object& json_config,
                                                     Server::Instance& server,
                                                     Upstream::ClusterManager& cluster_manager) {
  if (json_config.hasObject("cluster_name")) {
    const std::string cluster_name = json_config.getString("cluster_name");
    ENVOY_LOG(info, "statsd TCP cluster: {}", cluster_name);
    return Stats::SinkPtr(new Stats::Statsd::TcpStatsdSink(
        server.localInfo(), cluster_name, server.threadLocal(), cluster_manager, server.stats()));
  }

  throw EnvoyException(
      fmt::format("Didn't find cluster_name in the {} Stats::Sink config", name()));
}

std::string TcpStatsdSinkFactory::name() { return "statsd_tcp"; }

/**
 * Static registration for the tcp statsd sink. @see RegisterFactory.
 */
static Registry::RegisterFactory<TcpStatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
