#pragma once

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace EnvoyMobileMetricsService {

/**
 * Config registration for the EnvoyMobileMetricsService stats sink. @see StatsSinkFactory.
 */
class EnvoyMobileMetricsServiceSinkFactory : Logger::Loggable<Logger::Id::config>,
                                             public Server::Configuration::StatsSinkFactory {
public:
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

DECLARE_FACTORY(EnvoyMobileMetricsServiceSinkFactory);

} // namespace EnvoyMobileMetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
