#include "common/common/assert.h"

#include "extensions/filters/network/common/redis/fault_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::InSequence;
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
  void createCommandFault(RedisProxy::RedisFault* fault, std::string command_str,
                          int fault_percentage, int delay_seconds,
                          envoy::type::v3::FractionalPercent_DenominatorType denominator) {
    // We don't set fault type as it isn't used in the test

    auto* commands = fault->mutable_commands();
    auto* command = commands->Add();
    command->assign(command_str);

    addFaultPercentage(fault, fault_percentage, denominator);
    addDelay(fault, delay_seconds);
  }

  void createAllKeyFault(RedisProxy::RedisFault* fault, int fault_percentage, int delay_seconds,
                         envoy::type::v3::FractionalPercent_DenominatorType denominator) {
    addFaultPercentage(fault, fault_percentage, denominator);
    addDelay(fault, delay_seconds);
  }

  void addFaultPercentage(RedisProxy::RedisFault* fault, int fault_percentage,
                          envoy::type::v3::FractionalPercent_DenominatorType denominator) {
    envoy::config::core::v3::RuntimeFractionalPercent* fault_enabled =
        fault->mutable_fault_enabled();
    fault_enabled->set_runtime_key("runtime_key");
    auto* percentage = fault_enabled->mutable_default_value();
    percentage->set_numerator(fault_percentage);
    percentage->set_denominator(denominator);
  }

  void addDelay(RedisProxy::RedisFault* fault, int delay_seconds) {
    std::chrono::seconds duration = std::chrono::seconds(delay_seconds);
    fault->mutable_delay()->set_seconds(duration.count());
  }

  testing::NiceMock<Runtime::MockRandomGenerator> random_;
  Runtime::MockLoader runtime_;
};

TEST_F(FaultTest, NoFaults) {
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();

  TestScopedRuntime scoped_runtime;
  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);
  ASSERT_EQ(fault_manager.numberOfFaults(), 0);

  absl::optional<std::pair<FaultType, std::chrono::milliseconds>> fault_opt =
      fault_manager.getFaultForCommand("get");
  ASSERT_FALSE(fault_opt.has_value());
}

TEST_F(FaultTest, SingleCommandFaultNotEnabled) {
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "get", 0, 0, FractionalPercent::HUNDRED);

  TestScopedRuntime scoped_runtime;
  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);
  ASSERT_EQ(fault_manager.numberOfFaults(), 1);

  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_CALL(runtime_, snapshot());
  absl::optional<std::pair<FaultType, std::chrono::milliseconds>> fault_opt =
      fault_manager.getFaultForCommand("get");
  ASSERT_FALSE(fault_opt.has_value());
}

TEST_F(FaultTest, SingleCommandFault) {
  // Inject a single fault. Notably we use a different denominator to test that code path; normally
  // we use FractionalPercent::HUNDRED.
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "ttl", 5000, 0, FractionalPercent::TEN_THOUSAND);

  TestScopedRuntime scoped_runtime;
  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);
  ASSERT_EQ(fault_manager.numberOfFaults(), 1);

  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_CALL(runtime_, snapshot());
  absl::optional<std::pair<FaultType, std::chrono::milliseconds>> fault_opt =
      fault_manager.getFaultForCommand("ttl");
  ASSERT_TRUE(fault_opt.has_value());
}

TEST_F(FaultTest, MultipleFaults) {
  // This creates 2 faults, but the map will have 3 entries, as each command points to
  // command specific faults AND the general fault.
  RedisProxy redis_config;
  auto* faults = redis_config.mutable_faults();
  createCommandFault(faults->Add(), "get", 25, 0, FractionalPercent::HUNDRED);
  createAllKeyFault(faults->Add(), 25, 2, FractionalPercent::HUNDRED);

  TestScopedRuntime scoped_runtime;
  FaultManagerImpl fault_manager = FaultManagerImpl(random_, runtime_, *faults);
  ASSERT_EQ(fault_manager.numberOfFaults(), 3);

  absl::optional<std::pair<FaultType, std::chrono::milliseconds>> fault_opt;

  // Get command - should have a fault 50% of time
  // For the first call we mock the random percentage to be 1%, which will give us the first fault
  // with 0s delay.
  EXPECT_CALL(random_, random()).WillOnce(Return(1));
  EXPECT_CALL(runtime_, snapshot());
  fault_opt = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_opt.has_value());
  ASSERT_EQ(fault_opt.value().second, std::chrono::milliseconds(0));

  // For the second call we mock the random percentage to be 25%, which will give us the case with
  // no fault.
  EXPECT_CALL(random_, random()).WillOnce(Return(25));
  EXPECT_CALL(runtime_, snapshot()).Times(2);
  fault_opt = fault_manager.getFaultForCommand("get");
  ASSERT_FALSE(fault_opt.has_value());

  // The same result is expected for 74%.
  EXPECT_CALL(random_, random()).WillOnce(Return(74));
  EXPECT_CALL(runtime_, snapshot()).Times(2);
  fault_opt = fault_manager.getFaultForCommand("get");
  ASSERT_FALSE(fault_opt.has_value());

  // For the final call we mock the random percentage to be 75%, which will give us the second fault
  // with 2s delay.
  EXPECT_CALL(random_, random()).WillOnce(Return(75));
  EXPECT_CALL(runtime_, snapshot()).Times(2);
  fault_opt = fault_manager.getFaultForCommand("get");
  ASSERT_TRUE(fault_opt.has_value());
  ASSERT_EQ(fault_opt.value().second, std::chrono::milliseconds(2000));
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
