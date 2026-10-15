#pragma once

#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

constexpr char DynamicModuleStatsSinkName[] = "envoy.stat_sinks.dynamic_modules";

/**
 * Config registration for the dynamic module stats sink.
 */
class DynamicModuleStatsSinkFactory : public Server::Configuration::StatsSinkFactory {
public:
  absl::StatusOr<Stats::SinkPtr>
  createStatsSink(const Protobuf::Message& config,
                  Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override { return DynamicModuleStatsSinkName; }
};

DECLARE_FACTORY(DynamicModuleStatsSinkFactory);

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
