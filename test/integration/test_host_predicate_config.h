#pragma once

#include "envoy/upstream/retry.h"

#include "test/integration/test_host_predicate.h"
#include "test/integration/test_host_predicate.pb.h"
#include "test/integration/test_host_predicate.pb.validate.h"

#include "gmock/gmock.h"

namespace Envoy {
class TestHostPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  std::string name() const override { return "envoy.test_host_predicate"; }

  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t) override {
    return std::make_shared<testing::NiceMock<TestHostPredicate>>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new ::test::integration::TestHostPredicate()};
  }
};
} // namespace Envoy
