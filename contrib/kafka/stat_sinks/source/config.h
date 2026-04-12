#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/instance.h"

#include "source/server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {

constexpr char KafkaStatsSinkName[] = "envoy.stat_sinks.kafka";

class KafkaStatsSinkFactory : Logger::Loggable<Logger::Id::config>,
                              public Server::Configuration::StatsSinkFactory {
public:
  absl::StatusOr<Stats::SinkPtr>
  createStatsSink(const Protobuf::Message& config,
                  Server::Configuration::ServerFactoryContext& server) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

DECLARE_FACTORY(KafkaStatsSinkFactory);

} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
