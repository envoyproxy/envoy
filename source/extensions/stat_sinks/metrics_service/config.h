#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/instance.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {

/**
 * Config registration for the MetricsService stats sink. @see StatsSinkFactory.
 */
class MetricsServiceSinkFactory : Logger::Loggable<Logger::Id::config>,
                                  public Server::Configuration::StatsSinkFactory {
public:
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Instance& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() override;
};

DECLARE_FACTORY(MetricsServiceSinkFactory);

} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
