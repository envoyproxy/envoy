#pragma once

#include "envoy/extensions/watchdog/profile_action/v3/profile_action.pb.h"
#include "envoy/server/guarddog_config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace ProfileAction {

class ProfileActionFactory : public Server::Configuration::GuardDogActionFactory {
public:
  ProfileActionFactory() = default;

  Server::Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      Server::Configuration::GuardDogActionFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProfileActionConfig>();
  }

  std::string name() const override { return "envoy.watchdog.profile_action"; }

private:
  using ProfileActionConfig = envoy::extensions::watchdog::profile_action::v3::ProfileActionConfig;
};

} // namespace ProfileAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
