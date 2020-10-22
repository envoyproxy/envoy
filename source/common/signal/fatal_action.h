#pragma once

#include <ostream>

#include "envoy/common/pure.h"
#include "envoy/server/fatal_action_config.h"
#include "envoy/server/instance.h"

namespace Envoy {
namespace FatalAction {

using FatalActionPtrList = std::list<Server::Configuration::FatalActionPtr>;

// A simple class which manages the Fatal Actions registered via the
// extension point.
class FatalActionManager {
public:
  FatalActionManager(FatalActionPtrList& safe_actions, FatalActionPtrList& unsafe_actions,
                     Server::Instance* server);

  const FatalActionPtrList& getSafeActions() const { return safe_actions_; }
  const FatalActionPtrList& getUnsafeActions() const { return unsafe_actions_; }
  Server::Instance* getServer() const { return server_; }

private:
  FatalActionPtrList safe_actions_;
  FatalActionPtrList unsafe_actions_;
  Server::Instance* server_;
};

} // namespace FatalAction
} // namespace Envoy
