#pragma once

#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Upstream {

class RetryExtensionFactoryContextImpl : public Upstream::RetryExtensionFactoryContext {
public:
  RetryExtensionFactoryContextImpl(Singleton::Manager& singleton_manager)
      : singleton_manager_(singleton_manager) {}

  // Upstream::RetryOptionsPredicateFactoryContext
  Singleton::Manager& singletonManager() override { return singleton_manager_; }

private:
  Singleton::Manager& singleton_manager_;
};

} // namespace Upstream
} // namespace Envoy
