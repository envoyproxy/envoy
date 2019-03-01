#include "server/init_manager_impl.h"

#include <functional>

#include "common/common/assert.h"

#define TRACE_INIT_MANAGER(fmt, ...)                                                               \
  ENVOY_LOG(debug, "InitManagerImpl({}): " fmt, description_, ##__VA_ARGS__)

namespace Envoy {
namespace Server {

InitManagerImpl::InitManagerImpl(absl::string_view description) : description_(description) {
  TRACE_INIT_MANAGER("constructor");
}

InitManagerImpl::~InitManagerImpl() { TRACE_INIT_MANAGER("destructor"); }

void InitManagerImpl::initialize(std::function<void()> callback) {
  ASSERT(state_ == State::NotInitialized);
  if (targets_.empty()) {
    TRACE_INIT_MANAGER("empty targets, initialized");
    callback();
    state_ = State::Initialized;
  } else {
    TRACE_INIT_MANAGER("initializing");
    callback_ = callback;
    state_ = State::Initializing;
    // Target::initialize(...) method can modify the list to remove the item currently
    // being initialized, so we increment the iterator before calling initialize.
    for (auto iter = targets_.begin(); iter != targets_.end();) {
      TargetWithDescription& target = *iter;
      ++iter;
      initializeTarget(target);
    }
  }
}

void InitManagerImpl::initializeTarget(TargetWithDescription& target) {
  TRACE_INIT_MANAGER("invoking initializeTarget {}", target.second);
  target.first->initialize([this, &target]() -> void {
    TRACE_INIT_MANAGER("completed initializeTarget {}", target.second);
    ASSERT(std::find(targets_.begin(), targets_.end(), target) != targets_.end());
    targets_.remove(target);
    if (targets_.empty()) {
      TRACE_INIT_MANAGER("initialized");
      state_ = State::Initialized;
      callback_();
    }
  });
}

void InitManagerImpl::registerTarget(Init::Target& target, absl::string_view description) {
  TRACE_INIT_MANAGER("registerTarget {}", description);
  ASSERT(state_ != State::Initialized);
  ASSERT(std::find(targets_.begin(), targets_.end(),
                   TargetWithDescription{&target, std::string(description)}) == targets_.end(),
         "Registered duplicate Init::Target");
  targets_.emplace_back(&target, std::string(description));
  if (state_ == State::Initializing) {
    initializeTarget(targets_.back());
  }
}

} // namespace Server
} // namespace Envoy
