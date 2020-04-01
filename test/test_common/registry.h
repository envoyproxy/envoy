#pragma once

#include "envoy/registry/registry.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Registry {

/**
 * Factory registration template for tests. This can be used to inject a mock or dummy version
 * of a factory for testing purposes. It will restore the original value, if any, when it goes
 * out of scope.
 */
template <class Base> class InjectFactory {
public:
  InjectFactory(Base& instance) : instance_(instance) {
    EXPECT_STRNE(instance.category().c_str(), "");
    displaced_ = Registry::FactoryRegistry<Base>::replaceFactoryForTest(instance_);
  }

  ~InjectFactory() {
    // Always remove the injected factory so that it's name is not registered.
    Registry::FactoryRegistry<Base>::removeFactoryForTest(instance_.name(), instance_.configType());

    if (displaced_) {
      // Replace any displaced factory (which had the same name OR type as the injected factory).
      Registry::FactoryRegistry<Base>::replaceFactoryForTest(*displaced_);
    }
  }

private:
  Base& instance_;
  Base* displaced_{};
};

} // namespace Registry
} // namespace Envoy
