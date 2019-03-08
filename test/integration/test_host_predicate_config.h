#pragma once

#include "envoy/upstream/retry.h"

#include "test/integration/test_host_predicate.h"

#include "gmock/gmock.h"

namespace Envoy {
class TestHostPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  std::string name() override { return "envoy.test_host_predicate"; }

  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t) override {
    return std::make_shared<testing::NiceMock<TestHostPredicate>>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }
};
} // namespace Envoy
