#pragma once

#include "envoy/server/instance.h"

#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DogStatsd {

// DogStatsD compatible statsd sink
constexpr char DogStatsdName[] = "envoy.stat_sinks.dog_statsd";

/**
 * Config registration for the DogStatsD compatible statsd sink. @see StatsSinkFactory.
 */
class DogStatsdSinkFactory : Logger::Loggable<Logger::Id::config>,
                             public Server::Configuration::StatsSinkFactory {
public:
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace DogStatsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
