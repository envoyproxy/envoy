#pragma once

#include <list>

#include "envoy/init/init.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of Init::Manager for use during post cluster manager init / pre listening.
 * Deprecated, use SafeInit::ManagerImpl instead.
 * TODO(mergeconflict): convert all Init::ManagerImpl uses to SafeInit::ManagerImpl.
 */
class InitManagerImpl : public Init::Manager, Logger::Loggable<Logger::Id::init> {
public:
  InitManagerImpl(absl::string_view description);
  ~InitManagerImpl() override;

  void initialize(std::function<void()> callback);

  // Init::Manager
  void registerTarget(Init::Target& target, absl::string_view description) override;
  State state() const override { return state_; }

private:
  using TargetWithDescription = std::pair<Init::Target*, std::string>;

  void initializeTarget(TargetWithDescription& target);

  std::list<TargetWithDescription> targets_;
  State state_{State::NotInitialized};
  std::function<void()> callback_;
  std::string description_; // For debug tracing.
};

} // namespace Server
} // namespace Envoy
