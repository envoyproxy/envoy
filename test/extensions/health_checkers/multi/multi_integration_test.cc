#include "source/extensions/health_checkers/multi/multi.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {
namespace {

class MultiHealthCheckerImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  MultiHealthCheckerImplTest()
      : cluster_(std::make_shared<NiceMock<Upstream::MockClusterMockPrioritySet>>()) {}

  void createChecker(const std::string& yaml) {
    const auto config = Upstream::parseHealthCheckFromV3Yaml(yaml);
    health_checker_ = std::make_shared<MultiHealthChecker>(*cluster_, config, server_context_);
  }

  void setupTwoTcpNoData() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.multi
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.multi.v3.Multi
        methods:
        - tcp_health_check: {}
        - tcp_health_check: {}
    )EOF";
    createChecker(yaml);
  }

  void setupTwoTcpNoDataWithNames() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.multi
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.multi.v3.Multi
        methods:
        - name: first
          tcp_health_check: {}
        - name: second
          tcp_health_check: {}
    )EOF";
    createChecker(yaml);
  }

  struct SessionMocks {
    Event::MockTimer* interval_timer;
    Event::MockTimer* timeout_timer;
    Network::MockClientConnection* connection;
  };

  SessionMocks expectSessionCreate() {
    SessionMocks s;
    s.interval_timer = new Event::MockTimer(&server_context_.dispatcher_);
    s.timeout_timer = new Event::MockTimer(&server_context_.dispatcher_);
    s.connection = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(s.connection))
        .RetiresOnSaturation();
    return s;
  }

  void respondSuccess(SessionMocks& s) {
    EXPECT_CALL(*s.connection, close(Network::ConnectionCloseType::Abort));
    EXPECT_CALL(*s.timeout_timer, disableTimer());
    EXPECT_CALL(*s.interval_timer, enableTimer(_, _));
    s.connection->raiseEvent(Network::ConnectionEvent::Connected);
  }

  void respondFailure(SessionMocks& s) {
    EXPECT_CALL(*s.timeout_timer, disableTimer());
    EXPECT_CALL(*s.interval_timer, enableTimer(_, _));
    s.connection->raiseEvent(Network::ConnectionEvent::RemoteClose);
  }

  SessionMocks triggerNextCheck(SessionMocks& s) {
    auto new_connection = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(server_context_.dispatcher_, createClientConnection_(_, _, _, _))
        .WillOnce(Return(new_connection))
        .RetiresOnSaturation();
    EXPECT_CALL(*s.timeout_timer, enableTimer(_, _));
    s.interval_timer->invokeCallback();
    SessionMocks next;
    next.interval_timer = s.interval_timer;
    next.timeout_timer = s.timeout_timer;
    next.connection = new_connection;
    return next;
  }

  uint64_t counterValue(const std::string& name) {
    return cluster_->info_->stats_store_.counter(name).value();
  }

  std::shared_ptr<NiceMock<Upstream::MockClusterMockPrioritySet>> cluster_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<MultiHealthChecker> health_checker_;
};

// Verifies that when both sub-checkers report healthy, the host stays healthy.
TEST_F(MultiHealthCheckerImplTest, BothCheckersHealthy) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();

  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];
  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  respondSuccess(s1);
  respondSuccess(s2);

  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// Verifies that when one sub-checker fails, the host is marked unhealthy,
// and that both must pass for the host to return to healthy.
TEST_F(MultiHealthCheckerImplTest, OneCheckerFailsHostUnhealthy) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();
  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];

  // First checker passes.
  respondSuccess(s1);
  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  // Second checker fails → aggregate is unhealthy.
  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// Verifies recovery: host transitions from unhealthy to healthy when ALL checkers pass.
TEST_F(MultiHealthCheckerImplTest, RecoveryRequiresAllCheckersPassing) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();
  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];

  // Initial round: first passes, second fails.
  respondSuccess(s1);
  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  // Next round: first passes, second passes → recovery.
  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  // Still unhealthy because second checker hasn't reported yet this round.
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s2b = triggerNextCheck(s2);
  respondSuccess(s2b);
  // Now both pass → healthy.
  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

// Verifies the aggregate HostStatusCb fires with appropriate transitions.
TEST_F(MultiHealthCheckerImplTest, CallbackFiresOnTransition) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  int callback_count = 0;
  Upstream::HealthTransition last_transition{};
  Upstream::HealthState last_state{};
  health_checker_->addHostCheckCompleteCb(
      [&](const Upstream::HostSharedPtr&, Upstream::HealthTransition transition,
          Upstream::HealthState state) {
        callback_count++;
        last_transition = transition;
        last_state = state;
      });

  health_checker_->start();

  // Both checkers pass - host was already healthy, so "Unchanged".
  respondSuccess(s1);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(Upstream::HealthTransition::Unchanged, last_transition);
  EXPECT_EQ(Upstream::HealthState::Healthy, last_state);

  respondSuccess(s2);
  EXPECT_EQ(2, callback_count);

  // Next round: second checker fails → transition to unhealthy.
  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  EXPECT_EQ(3, callback_count);

  auto s2b = triggerNextCheck(s2);
  respondFailure(s2b);
  EXPECT_EQ(4, callback_count);
  EXPECT_EQ(Upstream::HealthTransition::Changed, last_transition);
  EXPECT_EQ(Upstream::HealthState::Unhealthy, last_state);
}

// Verifies that without names, both sub-checkers share the cluster stats scope
// so their attempt/success/failure counters are aggregated.
TEST_F(MultiHealthCheckerImplTest, StatsWithoutName) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();

  // Both sub-checkers share the cluster scope, so both attempts land in the same counter.
  EXPECT_EQ(2UL, counterValue("health_check.attempt"));
  EXPECT_EQ(0UL, counterValue("health_check.success"));
  EXPECT_EQ(0UL, counterValue("health_check.failure"));

  respondSuccess(s1);
  EXPECT_EQ(1UL, counterValue("health_check.success"));

  respondFailure(s2);
  EXPECT_EQ(1UL, counterValue("health_check.failure"));

  // Total attempts stays at 2 (one per checker at start).
  EXPECT_EQ(2UL, counterValue("health_check.attempt"));
}

// Verifies that with names, each sub-checker gets its own stats scope
// so their counters are tracked independently.
TEST_F(MultiHealthCheckerImplTest, StatsWithName) {
  InSequence s;

  setupTwoTcpNoDataWithNames();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();

  // With names, the shared cluster-scope counter should be zero
  // (each checker uses its own named scope).
  EXPECT_EQ(0UL, counterValue("health_check.attempt"));

  // Each named scope has its counters under "health_check.name.<name>.health_check.<stat>".
  EXPECT_EQ(1UL, counterValue("health_check.name.first.health_check.attempt"));
  EXPECT_EQ(1UL, counterValue("health_check.name.second.health_check.attempt"));

  respondSuccess(s1);
  EXPECT_EQ(1UL, counterValue("health_check.name.first.health_check.success"));
  EXPECT_EQ(0UL, counterValue("health_check.name.second.health_check.success"));

  respondFailure(s2);
  EXPECT_EQ(0UL, counterValue("health_check.name.second.health_check.success"));
  EXPECT_EQ(1UL, counterValue("health_check.name.second.health_check.failure"));

  // Cross-check: the first checker has no failures.
  EXPECT_EQ(0UL, counterValue("health_check.name.first.health_check.failure"));
}

} // namespace
} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
