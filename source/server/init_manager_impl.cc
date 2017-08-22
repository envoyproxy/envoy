#include "server/init_manager_impl.h"

#include <functional>

#include "common/common/assert.h"

namespace Envoy {
namespace Server {

void InitManagerImpl::initialize(std::function<void()> callback) {
  ASSERT(state_ == State::NotInitialized);
  if (targets_.empty()) {
    callback();
    state_ = State::Initialized;
  } else {
    callback_ = callback;
    state_ = State::Initializing;
    // Target::initialize(...) method can modify the list to remove the item currently
    // being initialized, so we increment the iterator before calling initialize.
    for (auto iter = targets_.begin(); iter != targets_.end();) {
      Init::Target* target = *iter;
      ++iter;
      initializeTarget(*target);
    }
  }
}

void InitManagerImpl::initializeTarget(Init::Target& target) {
  target.initialize([this, &target]() -> void {
    ASSERT(std::find(targets_.begin(), targets_.end(), &target) != targets_.end());
    targets_.remove(&target);
    if (targets_.empty()) {
      state_ = State::Initialized;
      callback_();
    }
  });
}

void InitManagerImpl::registerTarget(Init::Target& target) {
  ASSERT(state_ != State::Initialized);
  targets_.push_back(&target);
  if (state_ == State::Initializing) {
    initializeTarget(target);
  }
}

} // namespace Server
} // namespace Envoy
