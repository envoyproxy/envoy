#pragma once

#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

class MockBrokerFilterConfig : public BrokerFilterConfig {
public:
  MOCK_METHOD((const std::string&), stat_prefix, (), (const));
  MOCK_METHOD(bool, needsResponseRewrite, (), (const));
  MOCK_METHOD((absl::optional<HostAndPort>), findBrokerAddressOverride, (const uint32_t), (const));
  MOCK_METHOD((absl::flat_hash_set<int16_t>), apiKeysAllowed, (), (const));
  MOCK_METHOD((absl::flat_hash_set<int16_t>), apiKeysDenied, (), (const));
  MockBrokerFilterConfig() : BrokerFilterConfig{"prefix", false, {}, {}, {}} {};
};

using MockBrokerFilterConfigSharedPtr = std::shared_ptr<MockBrokerFilterConfig>;

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
