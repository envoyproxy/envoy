#pragma once

#include "envoy/server/instance.h"

#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Statsd {

// Statsd sink
constexpr char StatsdName[] = "envoy.stat_sinks.statsd";

/**
 * Config registration for the tcp statsd sink. @see StatsSinkFactory.
 */
class StatsdSinkFactory : Logger::Loggable<Logger::Id::config>,
                          public Server::Configuration::StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

DECLARE_FACTORY(StatsdSinkFactory);

} // namespace Statsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
