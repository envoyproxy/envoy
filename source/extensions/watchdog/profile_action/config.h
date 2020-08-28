#pragma once

#include "envoy/extensions/watchdog/profile_action/v3alpha/profile_action.pb.h"
#include "envoy/server/guarddog_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {

class ProfileActionFactory : public Server::Configuration::GuardDogActionFactory {
public:
  ProfileActionFactory() : name_("envoy.watchdog.profile_action"){};

  Server::Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      Server::Configuration::GuardDogActionFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProfileActionConfig>();
  }

  std::string name() const override { return name_; }

private:
  using ProfileActionConfig =
      envoy::extensions::watchdog::profile_action::v3alpha::ProfileActionConfig;

  const std::string name_;
};

} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
