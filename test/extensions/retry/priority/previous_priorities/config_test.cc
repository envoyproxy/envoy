#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/extensions/retry/priority/previous_priorities/config.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::StatusIs;

class RetryPriorityTest : public testing::Test {
public:
  void initialize(const Upstream::HealthyLoad& original_healthy_priority_load,
                  const Upstream::DegradedLoad& original_degraded_priority_load) {
    auto factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
        "envoy.retry_priorities.previous_priorities");

    envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig config;
    config.set_update_frequency(update_frequency_);
    // Use createEmptyConfigProto to exercise that code path. This ensures the proto returned
    // by that method is compatible with the downcast in createRetryPriorityFactory.
    auto empty = factory->createEmptyConfigProto();
    empty->MergeFrom(config);
    auto factory_cb_or_error =
        factory->createRetryPriorityFactory(*empty, ProtobufMessage::getStrictValidationVisitor());
    ASSERT_TRUE(factory_cb_or_error.ok()) << factory_cb_or_error.status();
    retry_priority_ = (*factory_cb_or_error)(3);
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
                           std::optional<Upstream::RetryPriority::PriorityMappingFunc>
                               priority_mapping_func = std::nullopt) {
    const auto& priority_loads = retry_priority_->determinePriorityLoad(
        &stream_info_, priority_set_, original_priority_load_,
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
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
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
      [&](const Upstream::HostDescription& host) -> std::optional<uint32_t> {
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
      [&](const Upstream::HostDescription& host) -> std::optional<uint32_t> {
    if (&host == host1.get()) {
      return 0;
    }
    ASSERT(&host == host2.get());
    return std::nullopt;
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

// Tests that an invalid frequency results in a config error at factory creation time.
TEST_F(RetryPriorityTest, OverriddenFrequencyInvalidValue) {
  auto* factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
      "envoy.retry_priorities.previous_priorities");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig config;
  config.set_update_frequency(0);
  auto cb_or_error =
      factory->createRetryPriorityFactory(config, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THAT(cb_or_error, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(cb_or_error, HasStatusMessage(AllOf(HasSubstr("Proto constraint validation failed"),
                                                  HasSubstr("value must be greater than"))));
}

// Tests that createRetryPriorityFactory validates the config once at config load time and returns
// a callback that creates working instances without retaining or re-using the ValidationVisitor.
TEST_F(RetryPriorityTest, CreateRetryPriorityFactoryValidatesOnceAndCreatesInstances) {
  auto* factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
      "envoy.retry_priorities.previous_priorities");
  ASSERT_NE(factory, nullptr);

  envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig config;
  config.set_update_frequency(2);
  Upstream::RetryPriorityFactoryCb factory_cb;
  {
    ProtobufMessage::StrictValidationVisitorImpl scoped_visitor;
    auto cb_or_error = factory->createRetryPriorityFactory(config, scoped_visitor);
    ASSERT_TRUE(cb_or_error.ok()) << cb_or_error.status();
    factory_cb = std::move(*cb_or_error);
  }

  // Invoking the factory callback after scoped_visitor is destroyed must succeed cleanly.
  retry_priority_ = factory_cb(3);
  ASSERT_NE(retry_priority_, nullptr);
  EXPECT_NE(retry_priority_, factory_cb(3));

  const Upstream::HealthyLoad original_priority_load({100, 0});
  const Upstream::DegradedLoad original_degraded_priority_load({0, 0});
  original_priority_load_ =
      Upstream::HealthyAndDegradedLoad{original_priority_load, original_degraded_priority_load};
  addHosts(0, 2, 2);
  addHosts(1, 2, 2);

  auto host1 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host1, priority()).WillByDefault(Return(0));
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(original_priority_load, original_degraded_priority_load);
  retry_priority_->onHostAttempted(host1);
  verifyPriorityLoads(Upstream::HealthyLoad({0, 100}), original_degraded_priority_load);
}

TEST_F(RetryPriorityTest, CreateRetryPriorityFactoryRejectsWrongConfigType) {
  auto* factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
      "envoy.retry_priorities.previous_priorities");
  ASSERT_NE(factory, nullptr);

  Protobuf::Struct wrong_config;
  auto cb_or_error = factory->createRetryPriorityFactory(
      wrong_config, ProtobufMessage::getStrictValidationVisitor());
  EXPECT_THAT(cb_or_error, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(cb_or_error, HasStatusMessage(HasSubstr("Invalid config type")));
}

// Tests that disabling the runtime guard defers validation to the returned callback without
// holding a reference to the config-load ValidationVisitor.
TEST_F(RetryPriorityTest, CreateRetryPriorityFactoryLegacyDeferredValidationWhenGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.validate_retry_priority_config_at_load", "false"}});

  auto* factory = Registry::FactoryRegistry<Upstream::RetryPriorityFactory>::getFactory(
      "envoy.retry_priorities.previous_priorities");
  ASSERT_NE(factory, nullptr);

  // Valid config creates working instances even after the config-load visitor is destroyed.
  envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig valid;
  valid.set_update_frequency(2);
  Upstream::RetryPriorityFactoryCb valid_cb;
  {
    ProtobufMessage::StrictValidationVisitorImpl scoped_visitor;
    auto cb_or_error = factory->createRetryPriorityFactory(valid, scoped_visitor);
    ASSERT_TRUE(cb_or_error.ok()) << cb_or_error.status();
    valid_cb = std::move(*cb_or_error);
  }
  retry_priority_ = valid_cb(3);
  ASSERT_NE(retry_priority_, nullptr);

  // Invalid config is accepted at factory creation time and throws when invoked.
  envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig invalid;
  invalid.set_update_frequency(0);
  auto invalid_cb_or_error =
      factory->createRetryPriorityFactory(invalid, ProtobufMessage::getStrictValidationVisitor());
  ASSERT_TRUE(invalid_cb_or_error.ok()) << invalid_cb_or_error.status();
  EXPECT_THROW_WITH_REGEX((*invalid_cb_or_error)(3), ProtoValidationException,
                          "Proto constraint validation failed.*value must be greater than.*");
}

// Regression test: RetryPolicyImpl rejects invalid PreviousPrioritiesConfig at config load time
// when the runtime guard is enabled and restores deferred validation when disabled.
TEST_F(RetryPriorityTest, RetryPolicyImplRejectsInvalidConfigAtLoad) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::route::v3::RetryPolicy retry_policy_proto;
  retry_policy_proto.set_retry_on("5xx");
  auto* retry_priority = retry_policy_proto.mutable_retry_priority();
  retry_priority->set_name("envoy.retry_priorities.previous_priorities");
  envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig
      priority_config;
  priority_config.set_update_frequency(0);
  ASSERT_TRUE(retry_priority->mutable_typed_config()->PackFrom(priority_config));

  auto policy_or_error = Router::RetryPolicyImpl::create(
      retry_policy_proto, ProtobufMessage::getStrictValidationVisitor(), factory_context);
  EXPECT_THAT(policy_or_error, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(policy_or_error, HasStatusMessage(HasSubstr("Proto constraint validation failed")));

  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.validate_retry_priority_config_at_load", "false"}});
  auto legacy_policy_or_error = Router::RetryPolicyImpl::create(
      retry_policy_proto, ProtobufMessage::getStrictValidationVisitor(), factory_context);
  ASSERT_TRUE(legacy_policy_or_error.ok()) << legacy_policy_or_error.status();
  EXPECT_THROW_WITH_REGEX((*legacy_policy_or_error)->retryPriority(), ProtoValidationException,
                          "Proto constraint validation failed");
}

} // namespace
} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
