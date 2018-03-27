#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the hystrix sink. @see StatsSinkFactory.
 */
class HystrixSinkFactory : Logger::Loggable<Logger::Id::config>, public StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config, Instance& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
