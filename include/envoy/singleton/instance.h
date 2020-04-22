#pragma once

#include <memory>

namespace Envoy {
namespace Singleton {

/**
 * All singletons must derive from this type.
 */
class Instance {
public:
  virtual ~Instance() = default;
};

using InstanceSharedPtr = std::shared_ptr<Instance>;

} // namespace Singleton
} // namespace Envoy
