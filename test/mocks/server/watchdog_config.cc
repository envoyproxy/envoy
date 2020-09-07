#include "watchdog_config.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::Return;

MockWatchdog::MockWatchdog(int miss, int megamiss, int kill, int multikill,
                           double multikill_threshold, const std::vector<std::string> action_protos)
    : miss_(miss), megamiss_(megamiss), kill_(kill), multikill_(multikill),
      multikill_threshold_(multikill_threshold), actions_([&]() {
        Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction> actions;

        for (const auto& action_proto_str : action_protos) {
          envoy::config::bootstrap::v3::Watchdog::WatchdogAction action;
          TestUtility::loadFromJson(action_proto_str, action);
          actions.Add()->CopyFrom(action);
        }

        return actions;
      }()) {
  ON_CALL(*this, missTimeout()).WillByDefault(Return(miss_));
  ON_CALL(*this, megaMissTimeout()).WillByDefault(Return(megamiss_));
  ON_CALL(*this, killTimeout()).WillByDefault(Return(kill_));
  ON_CALL(*this, multiKillTimeout()).WillByDefault(Return(multikill_));
  ON_CALL(*this, multiKillThreshold()).WillByDefault(Return(multikill_threshold_));
  ON_CALL(*this, actions).WillByDefault(Return(actions_));
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
