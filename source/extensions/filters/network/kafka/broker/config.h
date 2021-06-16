#pragma once

#include "envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.h"
#include "envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

constexpr char KafkaBrokerName[] = "envoy.filters.network.kafka_broker";

using KafkaBrokerProtoConfig = envoy::extensions::filters::network::kafka_broker::v3::KafkaBroker;

/**
 * Config registration for the Kafka filter.
 */
class KafkaConfigFactory : public Common::FactoryBase<KafkaBrokerProtoConfig> {
public:
  KafkaConfigFactory() : FactoryBase(KafkaBrokerName) {}

private:
  // Common::FactoryBase<KafkaBrokerProtoConfig>
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const KafkaBrokerProtoConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
