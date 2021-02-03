#pragma once

#include "envoy/upstream/retry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "retry_host_predicate.h"

namespace Envoy {
namespace Upstream {
using ::testing::NiceMock;
class TestRetryHostPredicateFactory : public RetryHostPredicateFactory {
public:
  RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&, uint32_t) override {
    return std::make_shared<NiceMock<MockRetryHostPredicate>>();
  }

  std::string name() const override { return "envoy.test_host_predicate"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }
};
} // namespace Upstream
} // namespace Envoy
