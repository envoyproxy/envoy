#include "init/manager_impl.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Init {

ManagerImpl::ManagerImpl(absl::string_view name)
    : name_(fmt::format("init manager {}", name)), state_(State::Uninitialized), count_(0),
      receiver_(name_, [this]() {
        if (--count_ == 0) {
          state_ = State::Initialized;
          caller_();
        }
      }) {}

Manager::State ManagerImpl::state() const { return state_; }

void ManagerImpl::add(const TargetReceiver& target_receiver) {
  ++count_;
  TargetCaller target_caller(target_receiver.caller(name_));
  switch (state_) {
  case State::Uninitialized:
    ENVOY_LOG(debug, "added {} to {}", target_receiver.name(), name_);
    targets_.add(std::move(target_caller));
    return;
  case State::Initializing:
    // It's important in this case that count_ was incremented before calling the target, because if
    // the target calls back to the receiver synchronously, count_ must have been incremented
    // before the receiver decrements and tests it.
    target_caller(receiver_);
    return;
  case State::Initialized:
    RELEASE_ASSERT(
        false, fmt::format("attempted to add {} to initialized {}", target_receiver.name(), name_));
  }
}

void ManagerImpl::initialize(const Receiver& receiver) {
  RELEASE_ASSERT(state_ == State::Uninitialized,
                 fmt::format("attempted to initialize {} twice", name_));

  if (count_ == 0) {
    ENVOY_LOG(debug, "{} contains no targets", name_);
    state_ = State::Initialized;
    receiver.caller(name_)();
  } else {
    ENVOY_LOG(debug, "{} initializing", name_);
    state_ = State::Initializing;
    caller_ = receiver.caller(name_);
    targets_(receiver_);
  }
}

} // namespace Init
} // namespace Envoy
