#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the udp statsd sink. @see StatsSinkFactory.
 */
class UdpStatsdSinkFactory : Logger::Loggable<Logger::Id::config>, public StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Json::Object& json_config, Instance& server,
                                 Upstream::ClusterManager& cluster_manager) override;

  std::string name() override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
