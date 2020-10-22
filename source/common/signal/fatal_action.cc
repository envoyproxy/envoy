#include "common/signal/fatal_action.h"

namespace Envoy {
namespace FatalAction {

FatalActionManager::FatalActionManager(FatalActionPtrList& safe_actions,
                                       FatalActionPtrList& unsafe_actions, Server::Instance* server)
    : server_(server) {
  // Transfer over the unique_ptrs
  for (Server::Configuration::FatalActionPtr& safe_action : safe_actions) {
    safe_actions_.push_back(std::move(safe_action));
  }
  safe_actions.clear();

  for (Server::Configuration::FatalActionPtr& unsafe_action : unsafe_actions) {
    unsafe_actions_.push_back(std::move(unsafe_action));
  }
  unsafe_actions.clear();
}

} // namespace FatalAction
} // namespace Envoy
