#pragma once

#include "envoy/singleton/manager.h"
#include "envoy/thread/thread.h"

#include "source/common/common/non_copyable.h"

#include "absl/container/node_hash_map.h"

namespace Envoy {
namespace Singleton {

/**
 * Implementation of the singleton manager that checks the registry for name validity. It is
 * assumed the singleton manager is only used on the main thread so it is not thread safe. Asserts
 * verify that.
 */
class ManagerImpl : public Manager, NonCopyable {
public:
  ManagerImpl() = default;

  // Singleton::Manager
  InstanceSharedPtr get(const std::string& name, SingletonFactoryCb cb, bool pin) override;

private:
  absl::node_hash_map<std::string, std::weak_ptr<Instance>> singletons_;
  std::vector<InstanceSharedPtr> pinned_singletons_;
};

} // namespace Singleton
} // namespace Envoy
