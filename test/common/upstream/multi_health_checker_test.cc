#include "source/common/upstream/multi_health_checker.h"

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
namespace Upstream {
namespace {

Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>
parseHealthChecksFromYaml(const std::vector<std::string>& yamls) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck> health_checks;
  for (const auto& yaml : yamls) {
    *health_checks.Add() = parseHealthCheckFromV3Yaml(yaml);
  }
  return health_checks;
}

class MultiHealthCheckerImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  MultiHealthCheckerImplTest()
      : cluster_(std::make_shared<NiceMock<MockClusterMockPrioritySet>>()) {}

  void createChecker(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_checks) {
    health_checker_ = std::make_shared<MultiHealthChecker>(*cluster_, health_checks, server_context_);
  }

  void setupTwoTcpNoData() {
    auto health_checks = parseHealthChecksFromYaml({
        R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    name: first
    tcp_health_check: {}
    )EOF",
        R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    name: second
    tcp_health_check: {}
    )EOF",
    });
    createChecker(health_checks);
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

  std::shared_ptr<NiceMock<MockClusterMockPrioritySet>> cluster_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  std::shared_ptr<MultiHealthChecker> health_checker_;
};

TEST_F(MultiHealthCheckerImplTest, MissingNameThrows) {
  auto health_checks = parseHealthChecksFromYaml({
      R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    name: first
    tcp_health_check: {}
    )EOF",
      R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    tcp_health_check: {}
    )EOF",
  });
  EXPECT_THROW_WITH_MESSAGE(createChecker(health_checks), EnvoyException,
                            "health check at index 1 is missing a name; all health checks "
                            "must have a name when multiple health checks are configured");
}

TEST_F(MultiHealthCheckerImplTest, FirstMissingNameThrows) {
  auto health_checks = parseHealthChecksFromYaml({
      R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    tcp_health_check: {}
    )EOF",
      R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    name: second
    tcp_health_check: {}
    )EOF",
  });
  EXPECT_THROW_WITH_MESSAGE(createChecker(health_checks), EnvoyException,
                            "health check at index 0 is missing a name; all health checks "
                            "must have a name when multiple health checks are configured");
}

TEST_F(MultiHealthCheckerImplTest, BothCheckersHealthy) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();

  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];
  EXPECT_FALSE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  respondSuccess(s1);
  respondSuccess(s2);

  EXPECT_FALSE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerImplTest, OneCheckerFailsHostUnhealthy) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();
  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];

  respondSuccess(s1);
  EXPECT_FALSE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerImplTest, RecoveryRequiresAllCheckersPassing) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();
  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];

  respondSuccess(s1);
  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  EXPECT_TRUE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s2b = triggerNextCheck(s2);
  respondSuccess(s2b);
  EXPECT_FALSE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerImplTest, CallbackFiresOnTransition) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  int callback_count = 0;
  HealthTransition last_transition{};
  HealthState last_state{};
  health_checker_->addHostCheckCompleteCb(
      [&](const HostSharedPtr&, HealthTransition transition, HealthState state) {
        callback_count++;
        last_transition = transition;
        last_state = state;
      });

  health_checker_->start();

  respondSuccess(s1);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(HealthTransition::Unchanged, last_transition);
  EXPECT_EQ(HealthState::Healthy, last_state);

  respondSuccess(s2);
  EXPECT_EQ(2, callback_count);

  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  EXPECT_EQ(3, callback_count);

  auto s2b = triggerNextCheck(s2);
  respondFailure(s2b);
  EXPECT_EQ(4, callback_count);
  EXPECT_EQ(HealthTransition::Changed, last_transition);
  EXPECT_EQ(HealthState::Unhealthy, last_state);
}

TEST_F(MultiHealthCheckerImplTest, BothFailPartialRecoveryStaysUnhealthy) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  auto s1 = expectSessionCreate();
  EXPECT_CALL(*s1.timeout_timer, enableTimer(_, _));
  auto s2 = expectSessionCreate();
  EXPECT_CALL(*s2.timeout_timer, enableTimer(_, _));

  health_checker_->start();
  auto& host = *cluster_->prioritySet().getMockHostSet(0)->hosts_[0];

  respondFailure(s1);
  respondFailure(s2);
  EXPECT_TRUE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s1b = triggerNextCheck(s1);
  respondSuccess(s1b);
  auto s2b = triggerNextCheck(s2);
  respondFailure(s2b);
  EXPECT_TRUE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  auto s1c = triggerNextCheck(s1b);
  respondSuccess(s1c);
  auto s2c = triggerNextCheck(s2b);
  respondSuccess(s2c);
  EXPECT_FALSE(host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerImplTest, MultipleHostsIndependentState) {
  InSequence s;

  setupTwoTcpNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80"),
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};

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
  EXPECT_FALSE(host1.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  respondSuccess(h2s1);
  respondFailure(h2s2);
  EXPECT_TRUE(host2.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  EXPECT_FALSE(host1.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
}

class FakeHealthChecker : public HealthChecker {
public:
  FakeHealthChecker(HealthFlagCallbacks& flag_callbacks) : flag_callbacks_(flag_callbacks) {}

  void addHostCheckCompleteCb(HostStatusCb callback) override {
    callbacks_.push_back(std::move(callback));
  }
  void start() override {}

  void reportResult(const HostSharedPtr& host, bool failed, bool degraded, bool pending = false) {
    if (failed) {
      flag_callbacks_.set(*host, Host::HealthFlag::FAILED_ACTIVE_HC);
    } else {
      flag_callbacks_.clear(*host, Host::HealthFlag::FAILED_ACTIVE_HC);
    }
    if (degraded) {
      flag_callbacks_.set(*host, Host::HealthFlag::DEGRADED_ACTIVE_HC);
    } else {
      flag_callbacks_.clear(*host, Host::HealthFlag::DEGRADED_ACTIVE_HC);
    }
    if (pending) {
      flag_callbacks_.set(*host, Host::HealthFlag::PENDING_ACTIVE_HC);
    } else {
      flag_callbacks_.clear(*host, Host::HealthFlag::PENDING_ACTIVE_HC);
    }

    auto state = failed ? HealthState::Unhealthy : HealthState::Healthy;
    for (const auto& cb : callbacks_) {
      cb(host, HealthTransition::Changed, state);
    }
  }

private:
  HealthFlagCallbacks& flag_callbacks_;
  std::vector<HostStatusCb> callbacks_;
};

class FakeHealthCheckerFactory : public Server::Configuration::CustomHealthCheckerFactory {
public:
  HealthCheckerSharedPtr
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
      : cluster_(std::make_shared<NiceMock<MockClusterMockPrioritySet>>()),
        inject_factory_(fake_factory_) {}

  void setup() {
    auto health_checks = parseHealthChecksFromYaml({
        R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    name: checker_a
    custom_health_check:
      name: envoy.health_checkers.fake
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
    )EOF",
        R"EOF(
    timeout: 1s
    interval: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    name: checker_b
    custom_health_check:
      name: envoy.health_checkers.fake
      typed_config:
        "@type": type.googleapis.com/google.protobuf.Struct
    )EOF",
    });

    health_checker_ = std::make_shared<MultiHealthChecker>(*cluster_, health_checks, server_context_);
  }

  std::shared_ptr<NiceMock<MockClusterMockPrioritySet>> cluster_;
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
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  ASSERT_EQ(2u, fake_factory_.instances_.size());
  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, false, false);
  checker_b->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  checker_a->reportResult(host, false, true);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  checker_b->reportResult(host, false, false);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));

  checker_a->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

TEST_F(MultiHealthCheckerDegradedTest, BothDegradedClearOneStillDegraded) {
  setup();
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, false, true);
  checker_b->reportResult(host, false, true);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));

  checker_a->reportResult(host, false, false);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));

  checker_b->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerDegradedTest, FailedAndDegradedCoexist) {
  setup();
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, true, false);
  checker_b->reportResult(host, false, true);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(0, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  checker_a->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  checker_b->reportResult(host, false, false);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC));
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

TEST_F(MultiHealthCheckerDegradedTest, PendingToStillPendingNoCallback) {
  setup();
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  ASSERT_EQ(2u, fake_factory_.instances_.size());
  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  int callback_count = 0;
  health_checker_->addHostCheckCompleteCb(
      [&](const HostSharedPtr&, HealthTransition, HealthState) { callback_count++; });

  checker_a->reportResult(host, false, false);

  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));

  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerDegradedTest, CheckerReportsWithPendingSet) {
  setup();
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  int callback_count = 0;
  health_checker_->addHostCheckCompleteCb(
      [&](const HostSharedPtr&, HealthTransition, HealthState) { callback_count++; });

  checker_a->reportResult(host, false, false, true);
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));

  checker_b->reportResult(host, false, false, true);
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));

  checker_a->reportResult(host, false, false);
  EXPECT_EQ(0, callback_count);
  EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));

  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC));
}

TEST_F(MultiHealthCheckerDegradedTest, PendingHostCountsAsHealthyInGauge) {
  setup();
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  host->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, false, false);
  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  checker_a->reportResult(host, true, false);
  EXPECT_EQ(0, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));

  checker_a->reportResult(host, false, false);
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

TEST_F(MultiHealthCheckerDegradedTest, DestructorDecrementsGauges) {
  setup();
  auto host = makeTestHost(cluster_->info_, "tcp://127.0.0.1:80");
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {host};
  health_checker_->start();

  auto* checker_a = fake_factory_.instances_[0];
  auto* checker_b = fake_factory_.instances_[1];

  checker_a->reportResult(host, false, true);
  checker_b->reportResult(host, false, false);
  EXPECT_EQ(1, gaugeValue("health_check.healthy"));
  EXPECT_EQ(1, gaugeValue("health_check.degraded"));

  health_checker_.reset();
  EXPECT_EQ(0, gaugeValue("health_check.healthy"));
  EXPECT_EQ(0, gaugeValue("health_check.degraded"));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
