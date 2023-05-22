#pragma once

#include "library/cc/engine_builder.h"

namespace Envoy {

// A wrapper class around EngineBuilder, specifically for mobile tests.
//
// Mobile tests often supply their own configuration for convenience, instead of using the
// EngineBuilder APIs. This wrapper class builds an Envoy Mobile Engine with the ability to
// access setOverrideConfigForTests(), which is a protected method inside EngineBuilder.
class TestEngineBuilder : public Platform::EngineBuilder {
public:
  virtual ~TestEngineBuilder() {}

  // Creates an Envoy Engine from the provided config and waits until the engine is running before
  // returning the Engine as a shared_ptr.
  Platform::EngineSharedPtr
  createEngine(std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> config);

  // Overrides the EngineBuilder's config with the provided string. Calls to the EngineBuilder's
  // bootstrap modifying APIs do not take effect after this function is called.
  void setOverrideConfig(std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap>&& config) {
    override_bootstrap_ = std::move(config);
  }

  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> generateBootstrap() const override {
    if (override_bootstrap_ != nullptr) {
      return std::move(override_bootstrap_);
    }
    return Platform::EngineBuilder::generateBootstrap();
  }

private:
  mutable std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> override_bootstrap_;
};

} // namespace Envoy
