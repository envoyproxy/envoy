#pragma once

#include <functional>
#include <memory>

#include "common/common/cleanup.h"

namespace Envoy {
namespace Common {

/**
 * Invokes a function for all elements in a container, providing a cleanup function that will be
 * executed after all the elements have been processed. The Cleanup object is provided to allow each
 * update callback to delay cleanup until some arbitrary time: the completion callback will be
 * invoked once no more references to the provided shared pointer exists.
 *
 * This provides a thread safe way of tracking the completion of callbacks based on a the elements
 * of a container that may require asynchronous processing.
 */
template <class ContainerT, class UpdateCbT>
void applyToAllWithCleanup(const ContainerT& container, UpdateCbT update_cb,
                           std::function<void()> done_cb) {
  auto cleanup = std::make_shared<Cleanup>(done_cb);
  for (auto element : container) {
    update_cb(element, cleanup);
  }
}
} // namespace Common
} // namespace Envoy