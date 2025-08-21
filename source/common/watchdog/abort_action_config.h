#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/watchdog/v3/abort_action.pb.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Watchdog {

class AbortActionFactory : public Server::Configuration::GuardDogActionFactory {
public:
  AbortActionFactory() = default;

  Server::Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      Server::Configuration::GuardDogActionFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<AbortActionConfig>();
  }

  std::string name() const override { return "envoy.watchdog.abort_action"; }

  using AbortActionConfig = envoy::watchdog::v3::AbortActionConfig;
};

DECLARE_FACTORY(AbortActionFactory);

} // namespace Watchdog
} // namespace Envoy
