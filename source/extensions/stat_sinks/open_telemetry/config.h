#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/instance.h"

#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

constexpr char OpenTelemetryName[] = "envoy.stat_sinks.open_telemetry";

/**
 * Config registration for the OpenTelemetry stats sink. @see StatsSinkFactory.
 */
class OpenTelemetrySinkFactory : Logger::Loggable<Logger::Id::config>,
                                 public Server::Configuration::StatsSinkFactory {
public:
  Stats::SinkPtr createStatsSink(const Protobuf::Message& config,
                                 Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

DECLARE_FACTORY(OpenTelemetrySinkFactory);

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
