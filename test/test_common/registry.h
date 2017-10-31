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
    displaced_ = Registry::FactoryRegistry<Base>::replaceFactoryForTest(instance_);
  }

  ~InjectFactory() {
    if (displaced_) {
      auto injected = Registry::FactoryRegistry<Base>::replaceFactoryForTest(*displaced_);
      EXPECT_EQ(injected, &instance_);
    } else {
      Registry::FactoryRegistry<Base>::removeFactoryForTest(instance_.name());
    }
  }

private:
  Base& instance_;
  Base* displaced_{};
};

} // namespace Registry
} // namespace Envoy
