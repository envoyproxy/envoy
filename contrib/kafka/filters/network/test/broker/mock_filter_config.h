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
  MockBrokerFilterConfig() : BrokerFilterConfig{"prefix", false, {}} {};
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
