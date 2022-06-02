#pragma once

#include "envoy/common/regex.h"
#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Regex {

// Wrapper for Engine instances returned by createBootstrapExtension() which must be implemented
// by all factories that derive RegexEngineBase.
class EngineExtension : public Server::BootstrapExtension {
public:
  EngineExtension(Engine& engine) : engine_(engine) {}

  // Server::BootstrapExtension
  void onServerInitialized() override {}

protected:
  Engine& engine_;
};

// Class to be derived by all Engine implementations.
class EngineBase : public Engine, public Server::Configuration::BootstrapExtensionFactory {};

static inline const Engine* engine(std::string name) {
  auto factory =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(name);
  return dynamic_cast<Engine*>(factory);
}

using EngineSingleton = InjectableSingleton<Engine>;
using EngineLoader = ScopedInjectableLoader<Engine>;

} // namespace Regex
} // namespace Envoy
