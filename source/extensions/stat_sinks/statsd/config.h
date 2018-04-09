#pragma once

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Statsd {

/**
 * Config registration for the tcp statsd sink. @see StatsSinkFactory.
 */
class StatsdSinkFactory : Logger::Loggable<Logger::Id::config>,
                          public Server::Configuration::StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Instance& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() override;
};

} // namespace Statsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
