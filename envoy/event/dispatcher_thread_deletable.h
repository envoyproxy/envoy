#pragma once

#include <memory>

namespace Envoy {
namespace Event {

/**
 * If an object derives from this class, it can be passed to the destination dispatcher who
 * guarantees to delete it in that dispatcher thread. The common use case is to ensure config
 * related objects are deleted in the main thread.
 */
class DispatcherThreadDeletable {
public:
  virtual ~DispatcherThreadDeletable() = default;
};

using DispatcherThreadDeletableConstPtr = std::unique_ptr<const DispatcherThreadDeletable>;

} // namespace Event
} // namespace Envoy
