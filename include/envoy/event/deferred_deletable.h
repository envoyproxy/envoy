#pragma once

#include <memory>

namespace Envoy {
namespace Event {

/**
 * If an object derives from this class, it can be passed to the dispatcher who guarantees to delete
 * it in a future event loop cycle. This allows clear ownership with unique_ptr while not having
 * to worry about stack unwind issues during event processing.
 */
class DeferredDeletable {
public:
  virtual ~DeferredDeletable() {}
};

typedef std::unique_ptr<DeferredDeletable> DeferredDeletablePtr;

} // namespace Event
} // namespace Envoy
