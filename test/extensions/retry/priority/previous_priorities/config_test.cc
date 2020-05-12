#include "envoy/config/retry/previous_priorities/previous_priorities_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/priority/previous_priorities/config.h"
#include "extensions/retry/priority/well_known_names.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {
namespace {

class RetryPriorityTest : public testing::Test {
public:
  void initialize(const Upstream::HealthyLoad& original_healthy_priority_load,
                  const Upstream::DegradedLoad& original_degraded_priority_load) {
    auto factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
        RetryPriorityValues::get().PreviousPrioritiesRetryPriority);

    envoy::config::retry::previous_priorities::PreviousPrioritiesConfig config;
    config.set_update_frequency(update_frequency_);
    // Use createEmptyConfigProto to exercise that code path. This ensures the proto returned
    // by that method is compatible with the downcast in createRetryPriority.
    auto empty = factory->createEmptyConfigProto();
    empty->MergeFrom(config);
    retry_priority_ =
        factory->createRetryPriority(*empty, ProtobufMessage::getStrictValidationVisitor(), 3);
    original_priority_load_ = Upstream::HealthyAndDegradedLoad{original_healthy_priority_load,
                                                               original_degraded_priority_load};
  }

  void addHosts(size_t priority, int count, int healthy_count, int degraded_count = 0) {
    auto host_set = priority_set_.getMockHostSet(priority);

    ASSERT(count >= healthy_count + degraded_count);

    host_set->hosts_.resize(count);
    host_set->healthy_hosts_.resize(healthy_count);
    host_set->degraded_hosts_.resize(degraded_count);
    host_set->runCallbacks({}, {});
  }

  void verifyPriorityLoads(const Upstream::HealthyLoad& expected_healthy_priority_load,
                           const Upstream::DegradedLoad& expected_degraded_priority_load,
                           absl::optional<Upstream::RetryPriority::PriorityMappingFunc>
                               priority_mapping_func = absl::nullopt) {
    const auto& priority_loads = retry_priority_->determinePriorityLoad(
        priority_set_, original_priority_load_,
        priority_mapping_func.value_or(Upstream::RetryPriority::defaultPriorityMapping));
    // Unwrapping gives a nicer gtest error.
    ASSERT_EQ(priority_loads.healthy_priority_load_.get(), expected_healthy_priority_load.get());
    ASSERT_EQ(priority_loads.degraded_priority_load_.get(), expected_degraded_priority_load.get());
  }

  std::vector<Upstream::MockHostSet> host_sets_;
  uint32_t update_frequency_{1};
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Upstream::RetryPrioritySharedPtr retry_priority_;
  Upstream::HealthyAndDegradedLoad original_priority_load_;
};

TEST_F(RetryPriorityTest, DefaultFrequency) {
  const Upstream::HealthyLoad original_priority_load({100, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 2, 2);
  addHosts(1, 2, 2);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  // Before any hosts attempted, load should be unchanged.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  const Upstream::HealthyLoad expected_priority_load({0, 100});
  const Upstream::DegradedLoad expected_degraded_priority_load({0, 0});

  // After attempting a host in P0, P1 should receive all the load.
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);

  // After we've tried host2, we've attempted all priorities and should reset back to the original
  // priority load.
  retry_priority_->onHostAttempted(host2);
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);
}

TEST_F(RetryPriorityTest, PriorityMappingCallback) {
  const Upstream::HealthyLoad original_priority_load({100, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 2, 2);
  addHosts(1, 2, 2);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_CALL(*host1, priority()).Times(0);

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_CALL(*host2, priority()).Times(0);

  Upstream::RetryPriority::PriorityMappingFunc priority_mapping_func =
      [&](const Upstream::HostDescription& host) -> absl::optional<uint32_t> {
    if (&host == host1.get()) {
      return 0;
    }
    ASSERT(&host == host2.get());
    return 1;
  };

  const Upstream::HealthyLoad expected_priority_load({0, 100});
  const Upstream::DegradedLoad expected_degraded_priority_load({0, 0});

  // After attempting a host in P0, P1 should receive all the load.
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load,
                      priority_mapping_func);

  // With a mapping function that doesn't recognize host2, results will remain the same as after
  // only trying host1.
  retry_priority_->onHostAttempted(host2);
  Upstream::RetryPriority::PriorityMappingFunc priority_mapping_func_no_host2 =
      [&](const Upstream::HostDescription& host) -> absl::optional<uint32_t> {
    if (&host == host1.get()) {
      return 0;
    }
    ASSERT(&host == host2.get());
    return absl::nullopt;
  };
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load,
                      priority_mapping_func_no_host2);

  // After we've tried host2, we've attempted all priorities and should reset back to the original
  // priority load.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load,
                      priority_mapping_func);
}

// Tests that we handle all hosts being unhealthy in the original priority set.
TEST_F(RetryPriorityTest, NoHealthyUpstreams) {
  const Upstream::HealthyLoad original_priority_load({0, 0, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 10, 0);
  addHosts(1, 10, 0);
  addHosts(2, 10, 0);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  auto host3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host3, priority()).WillByDefault(Return(2));

  // Before any hosts attempted, load should be unchanged.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  {
    // After attempting a host in P0, load should remain unchanged.
    const Upstream::HealthyLoad expected_priority_load({0, 0, 0});
    const Upstream::DegradedLoad expected_degraded_priority_load({0, 0, 0});

    retry_priority_->onHostAttempted(host1);
    verifyPriorityLoads(original_priority_load, original_degraded_priority_load);
  }
}

// Tests that spillover happens as we ignore attempted priorities.
TEST_F(RetryPriorityTest, DefaultFrequencyUnhealthyPriorities) {
  const Upstream::HealthyLoad original_priority_load({42, 28, 30});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0, 0});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 10, 3);
  addHosts(1, 10, 2);
  addHosts(2, 10, 10);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  auto host3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host3, priority()).WillByDefault(Return(2));

  // Before any hosts attempted, load should be unchanged.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  {
    // After attempting a host in P0, load should be split between P1 and P2 since P1 is degraded.
    const Upstream::HealthyLoad expected_priority_load({0, 28, 72});
    const Upstream::DegradedLoad expected_degraded_priority_load({0, 0, 0});
    retry_priority_->onHostAttempted(host1);
    verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);
  }

  // After we've tried host2, everything should go to P2.
  const Upstream::HealthyLoad expected_priority_load({0, 0, 100});
  const Upstream::DegradedLoad expected_degraded_priority_load({0, 0, 0});
  retry_priority_->onHostAttempted(host2);
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);

  // Once we've exhausted all priorities, we should return to the original load.
  retry_priority_->onHostAttempted(host3);
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);
}

// Tests that spillover happens as we ignore attempted priorities for degraded
// hosts.
TEST_F(RetryPriorityTest, DefaultFrequencyUnhealthyPrioritiesDegradedLoad) {
  const Upstream::HealthyLoad original_priority_load({0, 0, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({42, 28, 30});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 10, 0, 3);
  addHosts(1, 10, 0, 2);
  addHosts(2, 10, 0, 10);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  auto host3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host3, priority()).WillByDefault(Return(2));

  // Before any hosts attempted, load should be unchanged.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  {
    // After attempting a host in P0, load should be split between P1 and P2 since P1 is degraded.
    const Upstream::HealthyLoad expected_priority_load({0, 0, 0});
    const Upstream::DegradedLoad expected_degraded_priority_load({0, 28, 72});
    retry_priority_->onHostAttempted(host1);
    verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);
  }

  // After we've tried host2, everything should go to P2.
  const Upstream::HealthyLoad expected_priority_load({0, 0, 0});
  const Upstream::DegradedLoad expected_degraded_priority_load({0, 0, 100});
  retry_priority_->onHostAttempted(host2);
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);

  // Once we've exhausted all priorities, we should return to the original load.
  retry_priority_->onHostAttempted(host3);
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);
}

// Tests that we account for spillover between healthy and degraded priority load.
TEST_F(RetryPriorityTest, DefaultFrequencyUnhealthyPrioritiesDegradedLoadSpillover) {
  const Upstream::HealthyLoad original_priority_load({0, 100, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0, 0});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 10, 0, 3);
  addHosts(1, 10, 9, 1);
  addHosts(2, 10, 2, 0);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  auto host3 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host3, priority()).WillByDefault(Return(2));

  // Before any hosts attempted, load should be unchanged.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  {
    // After attempting a host in P1, load should be split between P2 (healthy),
    // and P0, P2 (degraded).
    const Upstream::HealthyLoad expected_priority_load({0, 0, 40});
    const Upstream::DegradedLoad expected_degraded_priority_load({60, 0, 0});
    retry_priority_->onHostAttempted(host2);
    verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);
  }

  // After we've tried host3, everything should go to P0 (degraded).
  const Upstream::HealthyLoad expected_priority_load({0, 0, 0});
  const Upstream::DegradedLoad expected_degraded_priority_load({100, 0, 0});
  retry_priority_->onHostAttempted(host3);
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);

  // Once we've exhausted all priorities, we should return to the original load.
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);
}

// Tests that we can override the frequency at which we update the priority load with the
// update_frequency parameter.
TEST_F(RetryPriorityTest, OverriddenFrequency) {
  update_frequency_ = 2;

  const Upstream::HealthyLoad original_priority_load({100, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0});

  initialize(original_priority_load, original_degraded_priority_load);
  addHosts(0, 2, 2);
  addHosts(1, 2, 2);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));

  auto host2 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host2, priority()).WillByDefault(Return(1));

  // Before any hosts attempted, load should be unchanged.
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  // After attempting a single host in P0, we should leave the priority load unchanged.
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);

  // After a second attempt, the priority load should change.
  const Upstream::HealthyLoad expected_priority_load({0, 100});
  const Upstream::DegradedLoad expected_degraded_priority_load({0, 0});
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(expected_priority_load, expected_degraded_priority_load);
}

// Tests that an invalid frequency results into a config error.
TEST_F(RetryPriorityTest, OverriddenFrequencyInvalidValue) {
  update_frequency_ = 0;

  const Upstream::HealthyLoad original_priority_load({100, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0});

  EXPECT_THROW_WITH_REGEX(initialize(original_priority_load, original_degraded_priority_load),
                          EnvoyException,
                          "Proto constraint validation failed.*value must be greater than.*");
}

} // namespace
} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
