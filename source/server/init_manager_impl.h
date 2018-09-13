#pragma once

#include <list>

#include "envoy/init/init.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of Init::Manager for use during post cluster manager init / pre listening.
 * TODO(JimmyCYJ): Move InitManagerImpl into a new subdirectory in source/ called init/.
 */
class InitManagerImpl : public Init::Manager {
public:
  void initialize(std::function<void()> callback);

  // Init::Manager
  void registerTarget(Init::Target& target) override;

private:
  enum class State { NotInitialized, Initializing, Initialized };

  void initializeTarget(Init::Target& target);

  std::list<Init::Target*> targets_;
  State state_{State::NotInitialized};
  std::function<void()> callback_;
};

} // namespace Server
} // namespace Envoy
