#pragma once

#include <ostream>

#include "envoy/common/pure.h"
#include "envoy/server/fatal_action_config.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace FatalAction {

// A simple class which manages the Fatal Actions registered via the
// extension point.
class FatalActionManager {
public:
  using FatalActionPtrList = std::list<const Server::Configuration::FatalActionPtr>;
  FatalActionManager() = delete;
  FatalActionManager(std::unique_ptr<FatalActionPtrList> safe_actions,
                     std::unique_ptr<FatalActionPtrList> unsafe_actions)
      : safe_actions_(std::move(safe_actions)), unsafe_actions_(std::move(unsafe_actions)) {}

  FatalActionPtrList& getSafeActions() const { return *safe_actions_; }
  FatalActionPtrList& getUnsafeActions() const { return *unsafe_actions_; }

private:
  std::unique_ptr<FatalActionPtrList> safe_actions_;
  std::unique_ptr<FatalActionPtrList> unsafe_actions_;
};

} // namespace FatalAction
} // namespace Envoy
