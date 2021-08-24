#pragma once

#include <functional>
#include <memory>

#include "source/common/common/cleanup.h"

namespace Envoy {
namespace Common {

/**
 * Invokes a function for all elements in a container and executes a done_cb once processing
 * is done for each element.
 *
 * To support cross-thread asynchronous work done for each element, the update_cb can extend
 * the lifetime of the provided Cleanup object until processing is done.
 */
template <class ElementT, class ContainerT>
void applyToAllWithCleanup(const ContainerT& container,
                           std::function<void(ElementT, std::shared_ptr<Cleanup>)> update_cb,
                           std::function<void()> done_cb) {
  // The Cleanup object is provided to allow each update callback to delay cleanup until some
  // arbitrary time the completion callback will be invoked once no more references to the provided
  // shared pointer exists.
  auto cleanup = std::make_shared<Cleanup>(done_cb);
  for (auto element : container) {
    update_cb(element, cleanup);
  }
} // namespace Common
} // namespace Common
} // namespace Envoy
