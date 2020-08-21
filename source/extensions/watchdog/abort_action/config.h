#pragma once

#include "envoy/extensions/watchdog/abort_action/v3alpha/abort_action.pb.h"
#include "envoy/server/guarddog_config.h"

#include "common/protobuf/protobuf.h"

#include "extensions/watchdog/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace AbortAction {

class AbortActionFactory : public Server::Configuration::GuardDogActionFactory {
public:
  AbortActionFactory() : name_(WatchdogActionNames::get().AbortAction){};

  Server::Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      Server::Configuration::GuardDogActionFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<AbortActionConfig>();
  }

  std::string name() const override { return name_; }

private:
  using AbortActionConfig = envoy::extensions::watchdog::abort_action::v3alpha::AbortActionConfig;
  const std::string name_;
};

} // namespace AbortAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
