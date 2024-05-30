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
  InjectFactory(Base& instance) : InjectFactory(instance, {}) {}

  InjectFactory(Base& instance, std::initializer_list<absl::string_view> deprecated_names)
      : instance_(instance) {
    EXPECT_STRNE(instance.category().c_str(), "");

    original_ = Registry::FactoryRegistry<Base>::getFactory(instance_.name());
    restore_factories_ =
        Registry::FactoryRegistry<Base>::replaceFactoryForTest(instance_, deprecated_names);
  }

  ~InjectFactory() {
    restore_factories_();

    auto* restored = Registry::FactoryRegistry<Base>::getFactory(instance_.name());
    ASSERT(restored == original_);
  }

  // Rebuilds the registry's factory-by-type mapping from scratch. In most cases, this is handled
  // by the replaceFactoryForTest calls in the constructor and destructor. This method is only
  // necessary if the disabled state of the factory is modified.
  static void resetTypeMappings() {
    Registry::FactoryRegistry<Base>::rebuildFactoriesByTypeForTest();
  }

  static void forceAllowDuplicates() { Registry::FactoryRegistry<Base>::allowDuplicates() = true; }

private:
  Base& instance_;
  Base* original_{};
  std::function<void()> restore_factories_;
};

/**
 * Registers a factory category for tests. Most tests do not need this functionality. It's only
 * useful for testing the registration infrastructure.
 */
template <class Base> class InjectFactoryCategory {
public:
  InjectFactoryCategory(Base& instance)
      : proxy_(std::make_unique<FactoryRegistryProxyImpl<Base>>()), instance_(instance) {
    // Register a new category.
    FactoryCategoryRegistry::registerCategory(instance_.category(), proxy_.get());
  }

  ~InjectFactoryCategory() {
    FactoryCategoryRegistry::deregisterCategoryForTest(instance_.category());
  }

private:
  std::unique_ptr<FactoryRegistryProxyImpl<Base>> proxy_;
  Base& instance_;
};

} // namespace Registry
} // namespace Envoy
