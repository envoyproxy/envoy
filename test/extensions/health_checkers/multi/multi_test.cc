#include "source/extensions/health_checkers/multi/multi.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/test_common/registry.h"
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

TEST(MultiHealthCheckerFactoryTest, CreateFromValidConfig) {
  const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    custom_health_check:
      name: envoy.health_checkers.multi
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.multi.v3.Multi
        health_checks:
        - name: first
          health_check:
            timeout: 1s
            interval: 1s
            unhealthy_threshold: 2
            healthy_threshold: 2
            http_health_check:
              path: /healthcheck
        - name: second
          health_check:
            timeout: 1s
            interval: 1s
            unhealthy_threshold: 2
            healthy_threshold: 2
            tcp_health_check: {}
    )EOF";

  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context;

  MultiHealthCheckerFactory factory;
  auto checker =
      factory.createCustomHealthChecker(Upstream::parseHealthCheckFromV3Yaml(yaml), context);
  EXPECT_NE(nullptr, checker.get());
}

TEST(MultiHealthCheckerFactoryTest, FactoryName) {
  MultiHealthCheckerFactory factory;
  EXPECT_EQ("envoy.health_checkers.multi", factory.name());
}

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
        health_checks:
        - name: first
          health_check:
            timeout: 1s
            interval: 1s
            unhealthy_threshold: 1
            healthy_threshold: 1
            tcp_health_check: {}
        - name: second
          health_check:
            timeout: 1s
            interval: 1s
            unhealthy_threshold: 1
            healthy_threshold: 1
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

  std::shared_ptr<NiceMock<Upstream::MockClusterMockPrioritySet>> cluster_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<MultiHealthChecker> health_checker_;
};

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

  respondSuccess(s1);
  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

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

  respondSuccess(s1);
  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s2b = triggerNextCheck(s2);
  respondSuccess(s2b);
  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

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
  health_checker_->addHostCheckCompleteCb([&](const Upstream::HostSharedPtr&,
                                              Upstream::HealthTransition transition,
                                              Upstream::HealthState state) {
    callback_count++;
    last_transition = transition;
    last_state = state;
  });

  health_checker_->start();

  respondSuccess(s1);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(Upstream::HealthTransition::Unchanged, last_transition);
  EXPECT_EQ(Upstream::HealthState::Healthy, last_state);

  respondSuccess(s2);
  EXPECT_EQ(2, callback_count);

  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  EXPECT_EQ(3, callback_count);

  auto s2b = triggerNextCheck(s2);
  respondFailure(s2b);
  EXPECT_EQ(4, callback_count);
  EXPECT_EQ(Upstream::HealthTransition::Changed, last_transition);
  EXPECT_EQ(Upstream::HealthState::Unhealthy, last_state);
}

TEST_F(MultiHealthCheckerImplTest, BothFailPartialRecoveryStaysUnhealthy) {
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

  respondFailure(s1);
  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  auto s2b = triggerNextCheck(s2);
  respondFailure(s2b);
  EXPECT_TRUE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s1c = triggerNextCheck(s1b);
  respondSuccess(s1c);
  auto s2c = triggerNextCheck(s2b);
  respondSuccess(s2c);
  EXPECT_FALSE(host.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerImplTest, MultipleHostsIndependentState) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80"),
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};

  auto h1s1 = expectSessionCreate();
  EXPECT_CALL(*h1s1.timeout_timer, enableTimer(_, _));
  auto h1s2 = expectSessionCreate();
  EXPECT_CALL(*h1s2.timeout_timer, enableTimer(_, _));
  auto h2s1 = expectSessionCreate();
  EXPECT_CALL(*h2s1.timeout_timer, enableTimer(_, _));
  auto h2s2 = expectSessionCreate();
  EXPECT_CALL(*h2s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();

  auto& host1 = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];
  auto& host2 = *cluster_->prioritySet().getMockHostSet(0)->hosts_[1];

  respondSuccess(h1s1);
  respondSuccess(h1s2);
  EXPECT_FALSE(host1.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  respondSuccess(h2s1);
  respondFailure(h2s2);
  EXPECT_TRUE(host2.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));

  EXPECT_FALSE(host1.healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
}

class FakeHealthChecker : public Upstream::HealthChecker {
public:
  FakeHealthChecker(Upstream::HealthFlagCallbacks flag_callbacks)
      : flag_callbacks_(std::move(flag_callbacks)) {}

  void addHostCheckCompleteCb(HostStatusCb callback) override {
    callbacks_.push_back(std::move(callback));
  }
  void start() override {}

  void reportResult(const Upstream::HostSharedPtr& host, bool failed, bool degraded,
                    bool pending = false) {
    if (failed) {
      flag_callbacks_.set(*host, Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
    } else {
      flag_callbacks_.clear(*host, Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
    }
    if (degraded) {
      flag_callbacks_.set(*host, Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
    } else {
      flag_callbacks_.clear(*host, Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
    }
    if (pending) {
      flag_callbacks_.set(*host, Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
    } else {
      flag_callbacks_.clear(*host, Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
    }

    auto state = failed ? Upstream::HealthState::Unhealthy : Upstream::HealthState::Healthy;
    for (const auto& cb : callbacks_) {
      cb(host, Upstream::HealthTransition::Changed, state);
    }
  }

private:
  Upstream::HealthFlagCallbacks flag_callbacks_;
  std::vector<HostStatusCb> callbacks_;
};

class FakeHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  Upstream::HealthCheckerSharedPtr
  createCustomHealthChecker(const envoy::config::core::v3::HealthCheck&,
                            Server::Configuration::HealthCheckerFactoryContext& context) override {
    auto checker = std::make_shared<FakeHealthChecker>(context.healthFlagCallbacks());
    instances_.push_back(checker.get());
    return checker;
  }

  std::string name() const override { return "envoy.health_checkers.fake"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::vector<FakeHealthChecker*> instances_;
};

class MultiHealthCheckerDegradedTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  MultiHealthCheckerDegradedTest()
      : cluster_(std::make_shared<NiceMock<Upstream::MockClusterMockPrioritySet>>()),
        inject_factory_(fake_factory_) {}

  void setup() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.multi
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.multi.v3.Multi
        health_checks:
        - name: checker_a
          health_check:
            timeout: 1s
            interval: 1s
            unhealthy_threshold: 1
            healthy_threshold: 1
            custom_health_check:
              name: envoy.health_checkers.fake
              typed_config:
                "@type": type.googleapis.com/google.protobuf.Struct
        - name: checker_b
          health_check:
            timeout: 1s
            interval: 1s
            unhealthy_threshold: 1
            healthy_threshold: 1
            custom_health_check:
              name: envoy.health_checkers.fake
              typed_config:
                "@type": type.googleapis.com/google.protobuf.Struct
    )EOF";

    const auto config = Upstream::parseHealthCheckFromV3Yaml(yaml);
    health_checker_ = std::make_shared<MultiHealthChecker>(*cluster_, config, server_context_);
  }

  std::shared_ptr<NiceMock<Upstream::MockClusterMockPrioritySet>> cluster_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  FakeHealthCheckerFactory fake_factory_;
  Registry::InjectFactory<Server::Configuration::CustomHealthCheckerFactory> inject_factory_;

  uint64_t gaugeValue(const std::string& name) {
    auto gauge = cluster_->info_->stats_store_.findGaugeByString(name);
    ASSERT(gauge.has_value());
    return gauge->get().value();
  }
  std::shared_ptr<MultiHealthChecker> health_checker_;
};

TEST_F(MultiHealthCheckerDegradedTest, OneDegradedSetsAggregate) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  ASSERT_EQ(2u, fake_factory_.instances_.size());
  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, false, false);
  checker_b->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  // Degraded but not failed: both healthy and degraded (matching base HC semantics).
  checker_a->reportResult(host, false, true);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  checker_b->reportResult(host, false, false);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));

  checker_a->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

TEST_F(MultiHealthCheckerDegradedTest, BothDegradedClearOneStillDegraded) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, false, true);
  checker_b->reportResult(host, false, true);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));

  checker_a->reportResult(host, false, false);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));

  checker_b->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerDegradedTest, FailedAndDegradedCoexist) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  // Failed and degraded: not healthy, but still degraded.
  checker_a->reportResult(host, true, false);
  checker_b->reportResult(host, false, true);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(0, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  // Clear failure: now healthy and degraded (overlap).
  checker_a->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  // Clear degraded: only healthy.
  checker_b->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

// When the host starts with PENDING and only one sub-checker clears it,
// the aggregate remains pending and callbacks are suppressed.
TEST_F(MultiHealthCheckerDegradedTest, PendingToStillPendingNoCallback) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  ASSERT_EQ(2u, fake_factory_.instances_.size());
  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  int callback_count = 0;
  health_checker_->addHostCheckCompleteCb([&](const Upstream::HostSharedPtr&,
                                              Upstream::HealthTransition,
                                              Upstream::HealthState) { callback_count++; });

  // Only checker_a reports success. checker_b is still pending.
  checker_a->reportResult(host, false, false);

  // Aggregate is still pending (checker_b hasn't reported), so no callback fires.
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));

  // Now checker_b reports, clearing the last pending bit.
  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));
}

// A sub-checker reporting with PENDING still set keeps the pending bit for that checker.
TEST_F(MultiHealthCheckerDegradedTest, CheckerReportsWithPendingSet) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  int callback_count = 0;
  health_checker_->addHostCheckCompleteCb([&](const Upstream::HostSharedPtr&,
                                              Upstream::HealthTransition,
                                              Upstream::HealthState) { callback_count++; });

  // checker_a reports but keeps its own pending flag set.
  checker_a->reportResult(host, false, false, true);
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));

  // checker_b also reports with pending still set.
  checker_b->reportResult(host, false, false, true);
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));

  // Now both clear pending.
  checker_a->reportResult(host, false, false);
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));

  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC));
}

// Pending-but-not-failed hosts count as healthy in the gauge (matching `HealthCheckerImplBase`
// semantics).
TEST_F(MultiHealthCheckerDegradedTest, PendingHostCountsAsHealthyInGauge) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  // Host starts pending but not failed, so it counts as healthy.
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  // Both checkers report success, clearing pending. Still healthy.
  checker_a->reportResult(host, false, false);
  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  // One checker fails. No longer healthy.
  checker_a->reportResult(host, true, false);
  EXPECT_EQ(0, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  // Recovery.
  checker_a->reportResult(host, false, false);
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

// Destruction decrements the gauges for all tracked hosts.
TEST_F(MultiHealthCheckerDegradedTest, DestructorDecrementsGauges) {
  setup();
  auto host = Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  // Make host healthy and degraded (both gauges incremented).
  checker_a->reportResult(host, false, true);
  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  // Destroy the health checker. Gauges should return to 0.
  health_checker_.reset();
  EXPECT_EQ(0, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

} // namespace
} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
