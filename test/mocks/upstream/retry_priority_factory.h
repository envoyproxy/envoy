#pragma once

#include "envoy/upstream/retry.h"

#include "test/mocks/upstream/upstream_mocks.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "retry_priority.h"

namespace Envoy {
namespace Upstream {
using ::testing::NiceMock;
class MockRetryPriorityFactory : public RetryPriorityFactory {
public:
  MockRetryPriorityFactory(const MockRetryPriority& retry_priority)
      : retry_priority_(retry_priority) {}
  RetryPrioritySharedPtr createRetryPriority(const Protobuf::Message&,
                                             ProtobufMessage::ValidationVisitor&,
                                             uint32_t) override {
    return std::make_shared<NiceMock<MockRetryPriority>>(retry_priority_);
  }

  std::string name() const override { return "envoy.test_retry_priority"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::mocks::upstream::TestRetryPriorityConfig>();
  }

private:
  const MockRetryPriority& retry_priority_;
};
} // namespace Upstream

} // namespace Envoy
