#include "server/init_manager_impl.h"

#include <functional>

#include "common/common/assert.h"

#define TRACE_INIT_MANAGER_WITH_DESCRIPTION(fmt, description, ...)                                 \
  ENVOY_LOG(debug, "InitManagerImpl({}): " fmt, description, ##__VA_ARGS__)

#define TRACE_INIT_MANAGER(fmt, ...)                                                               \
  TRACE_INIT_MANAGER_WITH_DESCRIPTION(fmt, description_, ##__VA_ARGS__)

namespace Envoy {
namespace Server {

InitManagerImpl::InitManagerImpl(absl::string_view description)
    : self_(std::make_shared<InitManagerImpl*>(this)), description_(description) {
  TRACE_INIT_MANAGER("constructor");
}

InitManagerImpl::~InitManagerImpl() {
  TRACE_INIT_MANAGER("destructor");
  *self_ = nullptr;
}

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
  target.first->initialize([self = self_, &target]() -> void {
    if (*self != nullptr) {
      const std::string& description = (*self)->description_;
      TRACE_INIT_MANAGER_WITH_DESCRIPTION("completed initializeTarget {}", description,
                                          target.second);
      auto& targets = (*self)->targets_;
      ASSERT(std::find(targets.begin(), targets.end(), target) != targets.end());
      targets.remove(target);
      if (targets.empty()) {
        TRACE_INIT_MANAGER_WITH_DESCRIPTION("initialized", description);
        (*self)->state_ = State::Initialized;
        (*self)->callback_();
      }
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
