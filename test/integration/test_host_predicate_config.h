#pragma once

#include "envoy/upstream/retry.h"

#include "test/integration/test_host_predicate.h"

namespace Envoy {
class TestHostPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  std::string name() override { return "envoy.test_host_predicate"; }

  void createHostPredicate(Upstream::RetryHostPredicateFactoryCallbacks& callbacks,
                           const ProtobufWkt::Struct&) override {
    callbacks.addHostPredicate(std::make_shared<TestHostPredicate>());
  }
};
} // namespace Envoy
