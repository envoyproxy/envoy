#pragma once

#include "envoy/upstream/retry.h"

#include "test/integration/test_host_predicate.h"

namespace Envoy {
class TestHostPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  std::string name() override { return "envoy.test_host_predicate"; }

  void createHostPredicate(Upstream::RetryHostPredicateFactoryCallbacks& callbacks,
                           const Protobuf::Message&, uint32_t) override {
    callbacks.addHostPredicate(std::make_shared<TestHostPredicate>());
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }
};
} // namespace Envoy
