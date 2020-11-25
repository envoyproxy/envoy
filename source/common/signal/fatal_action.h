#pragma once

#include <ostream>

#include "envoy/common/pure.h"
#include "envoy/server/fatal_action_config.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace FatalAction {

using FatalActionPtrList = std::list<Server::Configuration::FatalActionPtr>;

// Status when trying to run the Fatal Actions.
enum class Status {
  Success,

  // We either haven't set up the Fatal Action manager, or we unregistered it
  // as the server terminated.
  ActionManagerUnset,

  // Another thread beat us to running the Fatal Actions.
  RunningOnAnotherThread,

  // We have already ran those actions on this thread.
  AlreadyRanOnThisThread,
};

// A simple class which manages the Fatal Actions registered via the
// extension point.
class FatalActionManager {
public:
  FatalActionManager(FatalActionPtrList safe_actions, FatalActionPtrList unsafe_actions,
                     Thread::ThreadFactory& thread_factory)
      : safe_actions_(std::move(safe_actions)), unsafe_actions_(std::move(unsafe_actions)),
        thread_factory_(thread_factory) {}

  const FatalActionPtrList& getSafeActions() const { return safe_actions_; }
  const FatalActionPtrList& getUnsafeActions() const { return unsafe_actions_; }
  Thread::ThreadFactory& getThreadFactory() const { return thread_factory_; }

private:
  FatalActionPtrList safe_actions_;
  FatalActionPtrList unsafe_actions_;
  Thread::ThreadFactory& thread_factory_;
};

} // namespace FatalAction
} // namespace Envoy
