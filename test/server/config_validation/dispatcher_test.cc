#include <chrono>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/common/thread.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/libevent.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/server/config_validation/api.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {

// Define fixture which allocates ValidationDispatcher.
class ConfigValidation : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ConfigValidation() {
    validation_ = std::make_unique<Api::ValidationImpl>(
        Thread::threadFactoryForTest(), stats_store_, test_time_.timeSystem(),
        Filesystem::fileSystemForTest(), random_generator_, bootstrap_);
    dispatcher_ = validation_->allocateDispatcher("test_thread");
  }

  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherPtr dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  testing::NiceMock<Random::MockRandomGenerator> random_generator_;
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;

private:
  // Using config validation API.
  std::unique_ptr<Api::ValidationImpl> validation_;
};

// Simple test which creates a connection to fake upstream client. This is to test if
// ValidationDispatcher can call createClientConnection without crashing.
TEST_P(ConfigValidation, CreateConnection) {
  Network::Address::InstanceConstSharedPtr address(
      Network::Test::getCanonicalLoopbackAddress(GetParam()));
  dispatcher_->createClientConnection(address, address, Network::Test::createRawBufferSocket(),
                                      nullptr, nullptr);
  SUCCEED();
}

TEST_P(ConfigValidation, CreateScaledTimer) {
  EXPECT_NE(dispatcher_->createScaledTimer(Event::ScaledTimerType::UnscaledRealTimerForTest, [] {}),
            nullptr);
  EXPECT_NE(dispatcher_->createScaledTimer(
                Event::ScaledTimerMinimum(Event::ScaledMinimum(UnitFloat(0.5f))), [] {}),
            nullptr);
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ConfigValidation,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace Envoy
