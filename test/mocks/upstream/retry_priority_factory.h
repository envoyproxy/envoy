#pragma once

#include "envoy/upstream/retry.h"

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
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

private:
  const MockRetryPriority& retry_priority_;
};
} // namespace Upstream

} // namespace Envoy
