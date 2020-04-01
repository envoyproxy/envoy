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

    original_ = Registry::FactoryRegistry<Base>::getFactory(instance_.name());
    restore_factories_ = Registry::FactoryRegistry<Base>::replaceFactoryForTest(instance_);
  }

  ~InjectFactory() {
    restore_factories_();

    auto* restored = Registry::FactoryRegistry<Base>::getFactory(instance_.name());
    ASSERT(restored == original_);
  }

private:
  Base& instance_;
  Base* original_{};
  std::function<void()> restore_factories_;
};

} // namespace Registry
} // namespace Envoy
