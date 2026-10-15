#pragma once

#include "envoy/extensions/watchdog/backtrace_action/v3/backtrace_action.pb.h"
#include "envoy/server/guarddog_config.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Watchdog {
namespace BacktraceAction {

class BacktraceActionFactory : public Server::Configuration::GuardDogActionFactory {
public:
  BacktraceActionFactory() = default;

  Server::Configuration::GuardDogActionPtr createGuardDogActionFromProto(
      const envoy::config::bootstrap::v3::Watchdog::WatchdogAction& config,
      Server::Configuration::GuardDogActionFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<BacktraceActionConfig>();
  }

  std::string name() const override { return "envoy.watchdog.backtrace_action"; }

private:
  using BacktraceActionConfig =
      envoy::extensions::watchdog::backtrace_action::v3::BacktraceActionConfig;
};

} // namespace BacktraceAction
} // namespace Watchdog
} // namespace Extensions
} // namespace Envoy
