#pragma once

#include "source/extensions/filters/network/common/factory_base.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "contrib/envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.h"
#include "contrib/envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

using KafkaBrokerProtoConfig = envoy::extensions::filters::network::kafka_broker::v3::KafkaBroker;

/**
 * Config registration for the Kafka filter.
 */
class KafkaConfigFactory : public Common::FactoryBase<KafkaBrokerProtoConfig> {
public:
  KafkaConfigFactory() : FactoryBase(NetworkFilterNames::get().KafkaBroker) {}

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
