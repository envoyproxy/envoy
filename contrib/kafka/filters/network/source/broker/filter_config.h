#pragma once

#include "source/common/common/assert.h"

#include "contrib/envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.h"
#include "contrib/envoy/extensions/filters/network/kafka_broker/v3/kafka_broker.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

using KafkaBrokerProtoConfig = envoy::extensions::filters::network::kafka_broker::v3::KafkaBroker;

// Minor helper structure that contains broker filter configuration.
// Right now this object is very anemic but it might contain some business logic in the future such
// as "Does this topic apply?".
struct BrokerFilterConfig {

  BrokerFilterConfig(const KafkaBrokerProtoConfig& proto_config)
      : BrokerFilterConfig{proto_config.stat_prefix()} {}

  // Visible for testing.
  BrokerFilterConfig(const std::string& stat_prefix) : stat_prefix_{stat_prefix} {
    ASSERT(!stat_prefix_.empty());
  };

  std::string stat_prefix_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
