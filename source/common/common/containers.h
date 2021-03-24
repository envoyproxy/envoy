#pragma once

#include <functional>
#include <memory>

namespace Envoy {
namespace Common {

/**
 * Invokes a function for all elements in a container that accepts a completion callback which will
 * be invoked asynchronously, invoking a final completion callback once all callbacks associated
 * with elements have been invoked.
 */
template <class ContainerT, class UpdateCbT>
void applyToAllWithCompletionCallback(const ContainerT& container, UpdateCbT update_cb,
                                      std::function<void()> done_cb) {

  auto remaining_elements = std::make_shared<uint64_t>(container.size());
  for (auto element : container) {
    update_cb(element, [remaining_elements, done_cb] {
      if (--(*remaining_elements) == 0) {
        done_cb();
      }
    });
  }
}
} // namespace Common
} // namespace Envoy