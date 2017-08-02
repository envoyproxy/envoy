#pragma once

#include <unordered_map>

#include "envoy/singleton/manager.h"

namespace Envoy {
namespace Singleton {

/**
 * Implementation of the singleton manager that checks the registry for name validity.
 */
class ManagerImpl : public Manager {
public:
  InstancePtr get(const std::string& name) override;
  void set(const std::string& name, InstancePtr singleton) override;

private:
  void verifyRegistration(const std::string& name);

  std::unordered_map<std::string, std::weak_ptr<Instance>> singletons_;
};

} // namespace Singleton
} // namespace Envoy
