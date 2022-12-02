#include <memory>

#include "envoy/api/api.h"

#include "source/extensions/health_checkers/thrift/thrift.h"
#include "source/extensions/health_checkers/thrift/utility.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/health_checkers/thrift/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

class ThriftHealthCheckerTest : public Event::TestUsingSimulatedTime,
                                public testing::Test,
                                public ClientFactory {
public:
  ThriftHealthCheckerTest()
      : cluster_(new NiceMock<Upstream::MockClusterMockPrioritySet>()),
        event_logger_(new Upstream::MockHealthCheckEventLogger()), api_(Api::createApiForTest()) {}

  void setup(const std::string& yaml) {
    const auto& health_check_config = Upstream::parseHealthCheckFromV3Yaml(yaml);
    const auto& thrift_config = getThriftHealthCheckConfig(
        health_check_config, ProtobufMessage::getStrictValidationVisitor());

    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
        Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80", simTime())};

    health_checker_ = std::make_shared<ThriftHealthChecker>(
        *cluster_, health_check_config, thrift_config, dispatcher_, runtime_,
        Upstream::HealthCheckEventLoggerPtr(event_logger_), *api_, *this);
  }

  void setup() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY
    )EOF";

    setup(yaml);
  }

  void setupAlwaysLogHealthCheckFailures() {
    // set always_log_health_check_failures
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    always_log_health_check_failures: true
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY
    )EOF";

    setup(yaml);
  }

  void setupDoNotReuseConnection() {
    // unset reuse_connection
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    reuse_connection: false
    custom_health_check:
      name: thrift
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.health_checkers.thrift.v3.Thrift
        method_name: ping
        transport: HEADER
        protocol: BINARY
    )EOF";

    setup(yaml);
  }

  void startHealthChecker() {
    EXPECT_NE(nullptr, health_checker_);
    expectSessionCreate();
    expectClientAndPingRequestCreate();
    health_checker_->start();

    client_->runHighWatermarkCallbacks();
    client_->runLowWatermarkCallbacks();
  }

  void continueHealthCheck() {
    expectPingRequestCreate();
    interval_timer_->invokeCallback();
  }

  void restartHealthCheckSession() {
    expectClientAndPingRequestCreate();
    interval_timer_->invokeCallback();
  }

  void responseSuccess() {
    EXPECT_CALL(*timeout_timer_, disableTimer());
    EXPECT_CALL(*interval_timer_, enableTimer(_, _));
    client_->raiseResponseResult(true);
  }

  void responseFailure() {
    EXPECT_CALL(*timeout_timer_, disableTimer());
    EXPECT_CALL(*interval_timer_, enableTimer(_, _));
    client_->raiseResponseResult(false);
  }

  void disconnectHealthCheck() {
    EXPECT_CALL(*timeout_timer_, disableTimer());
    EXPECT_CALL(*interval_timer_, enableTimer(_, _));
    client_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  }

  void timeout() {
    EXPECT_CALL(*timeout_timer_, disableTimer());
    EXPECT_CALL(*interval_timer_, enableTimer(_, _));
    timeout_timer_->invokeCallback();
  }

  ClientPtr create(ClientCallback& callbacks, NetworkFilters::ThriftProxy::TransportType transport,
                   NetworkFilters::ThriftProxy::ProtocolType protocol,
                   const std::string& method_name, Upstream::HostSharedPtr, int32_t seq_id,
                   bool fixed_seq_id) override {
    EXPECT_EQ(transport, NetworkFilters::ThriftProxy::TransportType::Header);
    EXPECT_EQ(protocol, NetworkFilters::ThriftProxy::ProtocolType::Binary);
    EXPECT_EQ(method_name, "ping");
    EXPECT_EQ(seq_id, 0);
    EXPECT_TRUE(fixed_seq_id);
    return ClientPtr{create_(callbacks)};
  }

  MOCK_METHOD(Client*, create_, (ClientCallback&));

  void expectSessionCreate() {
    interval_timer_ = new Event::MockTimer(&dispatcher_);
    timeout_timer_ = new Event::MockTimer(&dispatcher_);
  }

  void expectClientAndPingRequestCreate() {
    EXPECT_CALL(*this, create_(_)).WillOnce(testing::Invoke([&](ClientCallback& callback) {
      client_ = new NiceMock<MockClient>(callback);
      expectPingRequestCreate();
      return client_;
    }));
  }

  void expectPingRequestCreate() {
    EXPECT_CALL(*client_, sendRequest()).WillOnce(Return(true));
    EXPECT_CALL(*timeout_timer_, enableTimer(_, _));
  }

  std::shared_ptr<Upstream::MockClusterMockPrioritySet> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  NiceMock<MockClient>* client_{};

  std::shared_ptr<ThriftHealthChecker> health_checker_;
  Api::ApiPtr api_;
};

TEST_F(ThriftHealthCheckerTest, Ping) {
  InSequence s;
  setup();

  startHealthChecker();
  responseSuccess();

  continueHealthCheck();
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  responseFailure();

  // Shutdown *without* an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

TEST_F(ThriftHealthCheckerTest, PingAndVariousFailures) {
  InSequence s;
  setup();

  startHealthChecker();
  responseSuccess();

  continueHealthCheck();
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  responseFailure();

  continueHealthCheck();
  disconnectHealthCheck();

  restartHealthCheckSession();
  // Close connection on timeout.
  EXPECT_CALL(*client_, close());
  timeout();

  restartHealthCheckSession();

  // Shutdown with an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

TEST_F(ThriftHealthCheckerTest, AlwaysLogHealthCheckFailures) {
  InSequence s;
  setupAlwaysLogHealthCheckFailures();

  startHealthChecker();
  responseSuccess();

  continueHealthCheck();
  // Fail on the exception response.
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, false));
  responseFailure();

  continueHealthCheck();
  // Fail again.
  EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, false));
  responseFailure();

  continueHealthCheck();
  // Shutdown with an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(4UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

// Host responses {EXCEPTION, SUCCESS}.
TEST_F(ThriftHealthCheckerTest, LogInitialFailure) {
  InSequence s;
  setup();

  startHealthChecker();
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, true));
  disconnectHealthCheck();

  restartHealthCheckSession();
  EXPECT_CALL(*event_logger_, logAddHealthy(_, _, false));
  responseSuccess();

  continueHealthCheck();

  // Shutdown with an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

// Host responses {SUCCESS, EXCEPTION, SUCCESS}.
TEST_F(ThriftHealthCheckerTest, LogTempFailureFailure) {
  InSequence s;
  setup();

  startHealthChecker();
  responseSuccess();

  continueHealthCheck();
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  disconnectHealthCheck();

  restartHealthCheckSession();
  EXPECT_CALL(*event_logger_, logAddHealthy(_, _, false));
  responseSuccess();

  continueHealthCheck();

  // Shutdown with an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(4UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

// Host responses {EXCEPTION, EXCEPTION}.
TEST_F(ThriftHealthCheckerTest, LogConsecutiveFailures) {
  InSequence s;
  setup();

  startHealthChecker();
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*event_logger_, logUnhealthy(_, _, _, true));
  responseFailure();

  continueHealthCheck();
  responseFailure();

  continueHealthCheck();

  // Shutdown with an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

// Tests that thrift client will behave appropriately when reuse_connection is false.
TEST_F(ThriftHealthCheckerTest, NoConnectionReuse) {
  InSequence s;
  setupDoNotReuseConnection();

  startHealthChecker();
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_, _));
  // The connection will be closed on success.
  EXPECT_CALL(*client_, close());
  // success response
  client_->raiseResponseResult(true);

  restartHealthCheckSession();
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_, _));
  // The connection will be closed on failure.
  EXPECT_CALL(*client_, close());
  client_->raiseResponseResult(false);
  responseFailure();

  restartHealthCheckSession();
  // Fail on disconnection. The connection was closed by the other end.
  disconnectHealthCheck();

  restartHealthCheckSession();
  // Timeout, the connection will be closed.
  EXPECT_CALL(*client_, close());
  timeout();

  restartHealthCheckSession();

  // Shutdown with an active request.
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(4UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
