#include "main.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace Configuration {

using ::testing::Return;

MockMain::MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill,
                   double wd_multikill_threshold, const std::vector<std::string> wd_action_protos)
    : wd_miss_(wd_miss), wd_megamiss_(wd_megamiss), wd_kill_(wd_kill), wd_multikill_(wd_multikill),
      wd_multikill_threshold_(wd_multikill_threshold), wd_actions_([&]() {
        Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction> actions;

        for (const auto& action_proto_str : wd_action_protos) {
          envoy::config::bootstrap::v3::Watchdog::WatchdogAction action;
          TestUtility::loadFromJson(action_proto_str, action);
          actions.Add()->CopyFrom(action);
        }

        return actions;
      }()) {
  ON_CALL(*this, wdMissTimeout()).WillByDefault(Return(wd_miss_));
  ON_CALL(*this, wdMegaMissTimeout()).WillByDefault(Return(wd_megamiss_));
  ON_CALL(*this, wdKillTimeout()).WillByDefault(Return(wd_kill_));
  ON_CALL(*this, wdMultiKillTimeout()).WillByDefault(Return(wd_multikill_));
  ON_CALL(*this, wdMultiKillThreshold()).WillByDefault(Return(wd_multikill_threshold_));
  ON_CALL(*this, wdActions).WillByDefault(Return(wd_actions_));
}

MockMain::~MockMain() = default;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
