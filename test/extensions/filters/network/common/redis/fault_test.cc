#include "envoy/common/random_generator.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/network/common/redis/fault_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

using RedisProxy = envoy::extensions::filters::network::redis_proxy::v3::RedisProxy;
using FractionalPercent = envoy::type::v3::FractionalPercent;
class FaultTest : public testing::Test {
public:
  const std::string RUNTIME_KEY = "runtime_key";

  void
  createCommandFault(RedisProxy::RedisFault* fault, std::string command_str, int delay_seconds,
                     absl::optional<int> fault_percentage,
                     absl::optional<envoy::type::v3::FractionalPercent_DenominatorType> denominator,
                     absl::optional<std::string> runtime_key) {
    // We don't set fault type as it isn't used in the test

    auto* commands = fault->mutable_commands();
    auto* command = commands->Add();
    command->assign(command_str);

    fault->set_fault_type(envoy::extensions::filters::network::redis_proxy::v3::
                              RedisProxy_RedisFault_RedisFaultType_ERROR);

    addFaultPercentage(fault, fault_percentage, denominator, runtime_key);
    addDelay(fault, delay_seconds);
  }

  void
  createAllKeyFault(RedisProxy::RedisFault* fault, int delay_seconds,
                    absl::optional<int> fault_percentage,
                    absl::optional<envoy::type::v3::FractionalPercent_DenominatorType> denominator,
                    absl::optional<std::string> runtime_key) {
    addFaultPercentage(fault, fault_percentage, denominator, runtime_key);
    addDelay(fault, delay_seconds);
  }

  void
  addFaultPercentage(RedisProxy::RedisFault* fault, absl::optional<int> fault_percentage,
                     absl::optional<envoy::type::v3::FractionalPercent_DenominatorType> denominator,
                     absl::optional<std::string> runtime_key) {
    envoy::config::core::v3::RuntimeFractionalPercent* fault_enabled =
        fault->mutable_fault_enabled();

    if (runtime_key.has_value()) {
      fault_enabled->set_runtime_key(runtime_key.value());
    }
    auto* percentage = fault_enabled->mutable_default_value();
    if (fault_percentage.has_value()) {
      percentage->set_numerator(fault_percentage.value());
    }
    if (denominator.has_value()) {
      percentage->set_denominator(denominator.value());
    }
  }

  void addDelay(RedisProxy::RedisFault* fault, int delay_seconds) {
    std::chrono::seconds duration = std::chrono::seconds(delay_seconds);
    fault->mutable_delay()->set_seconds(duration.count());
  }

  testing::NiceMock<Random::MockRandomGenerator> random_;
  testing::NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(FaultTest, MakeFaultForTestHelper) {
  Common::Redis::FaultSharedPtr fault_ptr =
      FaultManagerImpl::makeFaultForTest(FaultType::Error, std::chrono::milliseconds(10));

  ASSERT_TRUE(fault_ptr->faultType() == FaultType::Error);
  ASSERT_TRUE(fault_ptr->delayMs() == std::chrono::milliseconds(10));
}

TEST_F(FaultTest, NoFaults) {
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();

  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);

  const Fault* fault_ptr = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_ptr == nullptr);
}

TEST_F(FaultTest, SingleCommandFaultNotEnabled) {
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "get", 0, 0, FractionalPercent::HUNDRED, RUNTIME_KEY);

  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);

  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_CALL(runtime_, snapshot());
  const Fault* fault_ptr = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_ptr == nullptr);
}

TEST_F(FaultTest, SingleCommandFault) {
  // Inject a single fault. Notably we use a different denominator to test that code path; normally
  // we use FractionalPercent::HUNDRED.
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "ttl", 0, 5000, FractionalPercent::TEN_THOUSAND, RUNTIME_KEY);

  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger(RUNTIME_KEY, 50)).WillOnce(Return(10));

  const Fault* fault_ptr = fault_manager.getFaultForCommand("ttl");
  ASSERT_TRUE(fault_ptr != nullptr);
}

TEST_F(FaultTest, SingleCommandFaultWithNoDefaultValueOrRuntimeValue) {
  // Inject a single fault with no default value or runtime value.
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "ttl", 0, absl::nullopt, absl::nullopt, absl::nullopt);

  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  const Fault* fault_ptr = fault_manager.getFaultForCommand("ttl");
  ASSERT_TRUE(fault_ptr == nullptr);
}

TEST_F(FaultTest, MultipleFaults) {
  // This creates 2 faults, but the map will have 3 entries, as each command points to
  // command specific faults AND the general fault. The second fault has no runtime key,
  // forcing the runtime key check to be false in application code and falling back to the
  // default value.
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "get", 0, 25, FractionalPercent::HUNDRED, RUNTIME_KEY);
  createAllKeyFault(faults->Add(), 2, 25, FractionalPercent::HUNDRED, absl::nullopt);

  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);
  const Fault* fault_ptr;

  // Get command - should have a fault 50% of time
  // For the first call we mock the random percentage to be 10%, which will give us the first fault
  // with 0s delay.
  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, 25)).WillOnce(Return(10));
  fault_ptr = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_ptr != nullptr);
  ASSERT_EQ(fault_ptr->delayMs(), std::chrono::milliseconds(0));

  // Another Get; we mock the random percentage to be 25%, giving us the ALL_KEY fault
  EXPECT_CALL(random_, random()).WillOnce(Return(25));
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _))
      .Times(2)
      .WillOnce(Return(10))
      .WillOnce(Return(50));
  fault_ptr = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_ptr != nullptr);
  ASSERT_EQ(fault_ptr->delayMs(), std::chrono::milliseconds(2000));

  // No fault for Get command with mocked random percentage >= 50%.
  EXPECT_CALL(random_, random()).WillOnce(Return(50));
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _)).Times(2);
  fault_ptr = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_ptr == nullptr);

  // Any other command; we mock the random percentage to be 1%, giving us the ALL_KEY fault
  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _)).WillOnce(Return(10));

  fault_ptr = fault_manager.getFaultForCommand("ttl");
  ASSERT_TRUE(fault_ptr != nullptr);
  ASSERT_EQ(fault_ptr->delayMs(), std::chrono::milliseconds(2000));

  // No fault for any other command with mocked random percentage >= 25%.
  EXPECT_CALL(random_, random()).WillOnce(Return(25));
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _));
  fault_ptr = fault_manager.getFaultForCommand("ttl");
  ASSERT_TRUE(fault_ptr == nullptr);
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
