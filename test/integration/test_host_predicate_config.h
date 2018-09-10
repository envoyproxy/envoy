#pragma once

#include "envoy/upstream/retry.h"

#include "test/integration/test_host_predicate.h"

namespace Envoy {
class TestHostPredicateFactory : public Upstream::RetryHostPredicateFactory {
  std::string name() override { return "test-host-predicate"; }

  void createHostPredicate(Upstream::RetryHostPredicateFactoryCallbacks& callbacks) override {
    callbacks.addHostPredicate(std::make_shared<TestHostPredicate>());
  }
};
} // namespace Envoy
