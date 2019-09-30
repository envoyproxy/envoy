#pragma once

#include "envoy/config/filter/network/kafka_broker/v2/kafka_broker.pb.h"
#include "envoy/config/filter/network/kafka_broker/v2/kafka_broker.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

using KafkaBrokerProtoConfig = envoy::config::filter::network::kafka_broker::v2::KafkaBroker;

/**
 * Config registration for the Kafka filter.
 */
class KafkaConfigFactory : public Common::FactoryBase<KafkaBrokerProtoConfig> {
public:
  KafkaConfigFactory() : FactoryBase(NetworkFilterNames::get().KafkaBroker) {}

private:
  Network::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const KafkaBrokerProtoConfig& proto_config,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
