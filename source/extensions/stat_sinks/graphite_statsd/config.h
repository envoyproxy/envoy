#pragma once

#include "envoy/server/instance.h"

#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace GraphiteStatsd {

/**
 * Config registration for the tcp statsd sink. @see StatsSinkFactory.
 */
class GraphiteStatsdSinkFactory : Logger::Loggable<Logger::Id::config>,
                                  public Server::Configuration::StatsSinkFactory {
public:
  // GraphiteStatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

DECLARE_FACTORY(GraphiteStatsdSinkFactory);

} // namespace GraphiteStatsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
