#pragma once

#include "contrib/kafka/filters/network/source/kafka_request.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

class MockRequest : public AbstractRequest {
public:
  MockRequest(const int16_t api_key, const int16_t api_version, const int32_t correlation_id)
      : AbstractRequest{{api_key, api_version, correlation_id, ""}} {};
  uint32_t computeSize() const override { return 0; };
  uint32_t encode(Buffer::Instance&) const override { return 0; };
};

using MockRequestSharedPtr = std::shared_ptr<MockRequest>;

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
