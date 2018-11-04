#pragma once

#include <memory>

namespace Envoy {
namespace Singleton {

/**
 * All singletons must derive from this type.
 */
class Instance {
public:
  virtual ~Instance() {}
};

typedef std::shared_ptr<Instance> InstanceSharedPtr;

} // namespace Singleton
} // namespace Envoy
