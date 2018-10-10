#include "extensions/health_checkers/redis/redis.h"
#include "extensions/health_checkers/redis/utility.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace RedisHealthChecker {

class RedisHealthCheckerTest
    : public testing::Test,
      public Extensions::NetworkFilters::RedisProxy::ConnPool::ClientFactory {
public:
  RedisHealthCheckerTest()
      : cluster_(new NiceMock<Upstream::MockCluster>()),
        event_logger_(new Upstream::MockHealthCheckEventLogger()) {}

  void setup() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.redis
      config:
    )EOF";

    const auto& hc_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
    const auto& redis_config = getRedisHealthCheckConfig(hc_config);

    health_checker_.reset(
        new RedisHealthChecker(*cluster_, hc_config, redis_config, dispatcher_, runtime_, random_,
                               Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
  }

  void setupExistsHealthcheck() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    custom_health_check:
      name: envoy.health_checkers.redis
      config:
        key: foo
    )EOF";

    const auto& hc_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
    const auto& redis_config = getRedisHealthCheckConfig(hc_config);

    health_checker_.reset(
        new RedisHealthChecker(*cluster_, hc_config, redis_config, dispatcher_, runtime_, random_,
                               Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
  }

  void setupDontReuseConnection() {
    const std::string yaml = R"EOF(
    timeout: 1s
    interval: 1s
    no_traffic_interval: 5s
    interval_jitter: 1s
    unhealthy_threshold: 1
    healthy_threshold: 1
    reuse_connection: false
    custom_health_check:
      name: envoy.health_checkers.redis
      config:
    )EOF";

    const auto& hc_config = Upstream::parseHealthCheckFromV2Yaml(yaml);
    const auto& redis_config = getRedisHealthCheckConfig(hc_config);

    health_checker_.reset(
        new RedisHealthChecker(*cluster_, hc_config, redis_config, dispatcher_, runtime_, random_,
                               Upstream::HealthCheckEventLoggerPtr(event_logger_), *this));
  }

  Extensions::NetworkFilters::RedisProxy::ConnPool::ClientPtr
  create(Upstream::HostConstSharedPtr, Event::Dispatcher&,
         const Extensions::NetworkFilters::RedisProxy::ConnPool::Config&) override {
    return Extensions::NetworkFilters::RedisProxy::ConnPool::ClientPtr{create_()};
  }

  MOCK_METHOD0(create_, Extensions::NetworkFilters::RedisProxy::ConnPool::Client*());

  void expectSessionCreate() {
    interval_timer_ = new Event::MockTimer(&dispatcher_);
    timeout_timer_ = new Event::MockTimer(&dispatcher_);
  }

  void expectClientCreate() {
    client_ = new Extensions::NetworkFilters::RedisProxy::ConnPool::MockClient();
    EXPECT_CALL(*this, create_()).WillOnce(Return(client_));
    EXPECT_CALL(*client_, addConnectionCallbacks(_));
  }

  void expectExistsRequestCreate() {
    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthChecker::existsHealthCheckRequest("")), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    EXPECT_CALL(*timeout_timer_, enableTimer(_));
  }

  void expectPingRequestCreate() {
    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthChecker::pingHealthCheckRequest()), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    EXPECT_CALL(*timeout_timer_, enableTimer(_));
  }

  std::shared_ptr<Upstream::MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Upstream::MockHealthCheckEventLogger* event_logger_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Extensions::NetworkFilters::RedisProxy::ConnPool::MockClient* client_{};
  Extensions::NetworkFilters::RedisProxy::ConnPool::MockPoolRequest pool_request_;
  Extensions::NetworkFilters::RedisProxy::ConnPool::PoolCallbacks* pool_callbacks_{};
  std::shared_ptr<RedisHealthChecker> health_checker_;
};

TEST_F(RedisHealthCheckerTest, PingAndVariousFailures) {
  InSequence s;
  setup();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectPingRequestCreate();
  health_checker_->start();

  client_->runHighWatermarkCallbacks();
  client_->runLowWatermarkCallbacks();

  // Success
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Extensions::NetworkFilters::RedisProxy::RespValuePtr response(
      new Extensions::NetworkFilters::RedisProxy::RespValue());
  response->type(Extensions::NetworkFilters::RedisProxy::RespType::SimpleString);
  response->asString() = "PONG";
  pool_callbacks_->onResponse(std::move(response));

  expectPingRequestCreate();
  interval_timer_->callback_();

  // Failure
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Extensions::NetworkFilters::RedisProxy::RespValue());
  pool_callbacks_->onResponse(std::move(response));

  expectPingRequestCreate();
  interval_timer_->callback_();

  // Redis failure via disconnect
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  pool_callbacks_->onFailure();
  client_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate();
  expectPingRequestCreate();
  interval_timer_->callback_();

  // Timeout
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();

  expectClientCreate();
  expectPingRequestCreate();
  interval_timer_->callback_();

  // Shutdown with active request.
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

TEST_F(RedisHealthCheckerTest, Exists) {
  InSequence s;
  setupExistsHealthcheck();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectExistsRequestCreate();
  health_checker_->start();

  client_->runHighWatermarkCallbacks();
  client_->runLowWatermarkCallbacks();

  // Success
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Extensions::NetworkFilters::RedisProxy::RespValuePtr response(
      new Extensions::NetworkFilters::RedisProxy::RespValue());
  response->type(Extensions::NetworkFilters::RedisProxy::RespType::Integer);
  response->asInteger() = 0;
  pool_callbacks_->onResponse(std::move(response));

  expectExistsRequestCreate();
  interval_timer_->callback_();

  // Failure, exists
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Extensions::NetworkFilters::RedisProxy::RespValue());
  response->type(Extensions::NetworkFilters::RedisProxy::RespType::Integer);
  response->asInteger() = 1;
  pool_callbacks_->onResponse(std::move(response));

  expectExistsRequestCreate();
  interval_timer_->callback_();

  // Failure, no value
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Extensions::NetworkFilters::RedisProxy::RespValue());
  pool_callbacks_->onResponse(std::move(response));

  EXPECT_CALL(*client_, close());

  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
}

// Tests that redis client will behave appropriately when reuse_connection is false.
TEST_F(RedisHealthCheckerTest, NoConnectionReuse) {
  InSequence s;
  setupDontReuseConnection();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      Upstream::makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectPingRequestCreate();
  health_checker_->start();

  // The connection will close on success.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*client_, close());
  Extensions::NetworkFilters::RedisProxy::RespValuePtr response(
      new Extensions::NetworkFilters::RedisProxy::RespValue());
  response->type(Extensions::NetworkFilters::RedisProxy::RespType::SimpleString);
  response->asString() = "PONG";
  pool_callbacks_->onResponse(std::move(response));

  expectClientCreate();
  expectPingRequestCreate();
  interval_timer_->callback_();

  // The connection will close on failure.
  EXPECT_CALL(*event_logger_, logEjectUnhealthy(_, _, _));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*client_, close());
  response.reset(new Extensions::NetworkFilters::RedisProxy::RespValue());
  pool_callbacks_->onResponse(std::move(response));

  expectClientCreate();
  expectPingRequestCreate();
  interval_timer_->callback_();

  // Redis failure via disconnect, the connection was closed by the other end.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  pool_callbacks_->onFailure();
  client_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate();
  expectPingRequestCreate();
  interval_timer_->callback_();

  // Timeout, the connection will be closed.
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();

  expectClientCreate();
  expectPingRequestCreate();
  interval_timer_->callback_();

  // Shutdown with active request.
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());

  // The metrics expected after all tests have run.
  EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

} // namespace RedisHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
