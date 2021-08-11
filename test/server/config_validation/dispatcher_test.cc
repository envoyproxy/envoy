#include <chrono>

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
        Filesystem::fileSystemForTest(), random_generator_);
    dispatcher_ = validation_->allocateDispatcher("test_thread");
  }

  DangerousDeprecatedTestTime test_time_;
  Event::DispatcherPtr dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  testing::NiceMock<Random::MockRandomGenerator> random_generator_;

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
                                      nullptr);
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

// Make sure that creating DnsResolver does not cause crash and each call to create
// DNS resolver returns the same shared_ptr.
TEST_F(ConfigValidation, SharedDnsResolver) {
  std::vector<Network::Address::InstanceConstSharedPtr> resolvers;
  auto dns_resolver_options = envoy::config::core::v3::DnsResolverOptions();

  Network::DnsResolverSharedPtr dns1 =
      dispatcher_->createDnsResolver(resolvers, dns_resolver_options);
  long use_count = dns1.use_count();
  Network::DnsResolverSharedPtr dns2 =
      dispatcher_->createDnsResolver(resolvers, dns_resolver_options);

  EXPECT_EQ(dns1.get(), dns2.get());          // Both point to the same instance.
  EXPECT_EQ(use_count + 1, dns2.use_count()); // Each call causes ++ in use_count.
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ConfigValidation,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace Envoy
