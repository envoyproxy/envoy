#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/config/cds_json.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/redis/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Upstream {
namespace {

envoy::api::v2::HealthCheck parseHealthCheckFromJson(const std::string& json_string) {
  envoy::api::v2::HealthCheck health_check;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::CdsJson::translateHealthCheck(*json_object_ptr, health_check);
  return health_check;
}

TEST(HealthCheckerFactoryTest, createRedis) {
  std::string json = R"EOF(
  {
    "type": "redis",
    "timeout_ms": 1000,
    "interval_ms": 1000,
    "unhealthy_threshold": 1,
    "healthy_threshold": 1
  }
  )EOF";

  NiceMock<Upstream::MockCluster> cluster;
  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;
  EXPECT_NE(nullptr, dynamic_cast<RedisHealthCheckerImpl*>(
                         HealthCheckerFactory::create(parseHealthCheckFromJson(json), cluster,
                                                      runtime, random, dispatcher)
                             .get()));
}

// TODO(htuch): This provides coverage on MissingFieldException and missing health check type
// handling for HealthCheck construction, but should eventually be subsumed by whatever we do for
// #1308.
TEST(HealthCheckerFactoryTest, MissingFieldiException) {
  NiceMock<Upstream::MockCluster> cluster;
  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;
  envoy::api::v2::HealthCheck health_check;
  // No health checker type set
  EXPECT_THROW(HealthCheckerFactory::create(health_check, cluster, runtime, random, dispatcher),
               EnvoyException);
  health_check.mutable_http_health_check();
  // No timeout field set.
  EXPECT_THROW(HealthCheckerFactory::create(health_check, cluster, runtime, random, dispatcher),
               MissingFieldException);
}

class TestHttpHealthCheckerImpl : public HttpHealthCheckerImpl {
public:
  using HttpHealthCheckerImpl::HttpHealthCheckerImpl;

  Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& conn_data) override {
    return createCodecClient_(conn_data);
  };

  // HttpHealthCheckerImpl
  MOCK_METHOD1(createCodecClient_, Http::CodecClient*(Upstream::Host::CreateConnectionData&));
};

class HttpHealthCheckerImplTest : public testing::Test {
public:
  struct TestSession {
    TestSession() {}

    Event::MockTimer* interval_timer_{};
    Event::MockTimer* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Network::MockClientConnection* client_connection_{};
    NiceMock<Http::MockStreamEncoder> request_encoder_;
    Http::StreamDecoder* stream_response_callbacks_{};
  };

  typedef std::unique_ptr<TestSession> TestSessionPtr;

  HttpHealthCheckerImplTest() : cluster_(new NiceMock<MockCluster>()) {}

  void setupNoServiceValidationHC() {
    std::string json = R"EOF(
    {
      "type": "http",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "interval_jitter_ms": 1000,
      "unhealthy_threshold": 2,
      "healthy_threshold": 2,
      "path": "/healthcheck"
    }
    )EOF";

    health_checker_.reset(new TestHttpHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                        dispatcher_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr host, bool changed_state) -> void {
      onHostStatus(host, changed_state);
    });
  }

  void setupNoServiceValidationNoReuseConnectionHC() {
    std::string json = R"EOF(
    {
      "type": "http",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "interval_jitter_ms": 1000,
      "unhealthy_threshold": 2,
      "healthy_threshold": 2,
      "reuse_connection": false,
      "path": "/healthcheck"
    }
    )EOF";

    health_checker_.reset(new TestHttpHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                        dispatcher_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr host, bool changed_state) -> void {
      onHostStatus(host, changed_state);
    });
  }

  void setupServiceValidationHC() {
    std::string json = R"EOF(
    {
      "type": "http",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "service_name": "locations",
      "interval_jitter_ms": 1000,
      "unhealthy_threshold": 2,
      "healthy_threshold": 2,
      "path": "/healthcheck"
    }
    )EOF";

    health_checker_.reset(new TestHttpHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                        dispatcher_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr host, bool changed_state) -> void {
      onHostStatus(host, changed_state);
    });
  }

  void expectSessionCreate() {
    // Expectations are in LIFO order.
    TestSessionPtr new_test_session(new TestSession());
    test_sessions_.emplace_back(std::move(new_test_session));
    TestSession& test_session = *test_sessions_.back();
    test_session.timeout_timer_ = new Event::MockTimer(&dispatcher_);
    test_session.interval_timer_ = new Event::MockTimer(&dispatcher_);
    expectClientCreate(test_sessions_.size() - 1);
  }

  void expectClientCreate(size_t index) {
    TestSession& test_session = *test_sessions_[index];

    auto* codec = test_session.codec_ = new NiceMock<Http::MockClientConnection>();
    test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
    auto create_codec_client = [codec](Upstream::Host::CreateConnectionData& conn_data) {
      return new CodecClientForTest(std::move(conn_data.connection_), codec, nullptr, nullptr);
    };

    EXPECT_CALL(dispatcher_, createClientConnection_(_, _))
        .WillOnce(Return(test_session.client_connection_));
    EXPECT_CALL(*health_checker_, createCodecClient_(_)).WillOnce(Invoke(create_codec_client));
  }

  void expectStreamCreate(size_t index) {
    test_sessions_[index]->request_encoder_.stream_.callbacks_.clear();
    EXPECT_CALL(*test_sessions_[index]->codec_, newStream(_))
        .WillOnce(DoAll(SaveArgAddress(&test_sessions_[index]->stream_response_callbacks_),
                        ReturnRef(test_sessions_[index]->request_encoder_)));
  }

  void respond(size_t index, const std::string& code, bool conn_close, bool body = false,
               bool trailers = false,
               const Optional<std::string>& service_cluster = Optional<std::string>()) {
    std::unique_ptr<Http::TestHeaderMapImpl> response_headers(
        new Http::TestHeaderMapImpl{{":status", code}});
    if (service_cluster.valid()) {
      response_headers->addCopy(Http::Headers::get().EnvoyUpstreamHealthCheckedCluster,
                                service_cluster.value());
    }
    if (conn_close) {
      response_headers->addCopy("connection", "close");
    }

    test_sessions_[index]->stream_response_callbacks_->decodeHeaders(std::move(response_headers),
                                                                     !body && !trailers);
    if (body) {
      Buffer::OwnedImpl response_data;
      test_sessions_[index]->stream_response_callbacks_->decodeData(response_data, !trailers);
    }

    if (trailers) {
      test_sessions_[index]->stream_response_callbacks_->decodeTrailers(
          Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{"some", "trailer"}}});
    }
  }

  MOCK_METHOD2(onHostStatus, void(HostSharedPtr host, bool changed_state));

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<TestSessionPtr> test_sessions_;
  std::shared_ptr<TestHttpHealthCheckerImpl> health_checker_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(HttpHealthCheckerImplTest, Success) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessServiceCheck) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  Optional<std::string> health_checked_cluster("locations-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceDoesNotMatchFail) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  Optional<std::string> health_checked_cluster("api-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceNotPresentInResponseFail) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, true, false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceCheckRuntimeOff) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(false));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  Optional<std::string> health_checked_cluster("api-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedFailFirstServiceCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillRepeatedly(Return(true));
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->hosts_[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();
  Optional<std::string> health_checked_cluster("locations-production-iad");

  // Test that failing first disables fast success.
  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "503", false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessNoTraffic) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(60000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, true, true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedSuccessFirst) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->hosts_[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Test fast success immediately moves us to healthy.
  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _)).WillOnce(Return(500));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(500)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedFailFirst) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->hosts_[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Test that failing first disables fast success.
  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "503", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, HttpFail) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "503", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, Disconnect) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(cluster_->hosts_[0], true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, Timeout) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, DynamicAddAndRemove) {
  setupNoServiceValidationHC();
  health_checker_->start();

  expectSessionCreate();
  expectStreamCreate(0);
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  cluster_->runCallbacks({cluster_->hosts_.back()}, {});

  std::vector<HostSharedPtr> removed{cluster_->hosts_.back()};
  cluster_->hosts_.clear();
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  cluster_->runCallbacks({}, removed);
}

TEST_F(HttpHealthCheckerImplTest, ConnectionClose) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();
}

TEST_F(HttpHealthCheckerImplTest, RemoteCloseBetweenChecks) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

// Test that we close connections on a healthy check when reuse_connection is false.
TEST_F(HttpHealthCheckerImplTest, DontReuseConnectionBetweenChecks) {
  setupNoServiceValidationNoReuseConnectionHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  // A new client is created because we close the connection ourselves.
  // See HttpHealthCheckerImplTest.RemoteCloseBetweenChecks for how this works when the remote end
  // closes the connection.
  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, StreamReachesWatermarkDuringCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());

  test_sessions_[0]->request_encoder_.stream_.runHighWatermarkCallbacks();
  test_sessions_[0]->request_encoder_.stream_.runLowWatermarkCallbacks();

  respond(0, "200", true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ConnectionReachesWatermarkDuringCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());

  test_sessions_[0]->client_connection_->runHighWatermarkCallbacks();
  test_sessions_[0]->client_connection_->runLowWatermarkCallbacks();

  respond(0, "200", true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST(TcpHealthCheckMatcher, loadJsonBytes) {
  {
    Protobuf::RepeatedPtrField<envoy::api::v2::HealthCheck::Payload> repeated_payload;
    repeated_payload.Add()->set_text("39000000");
    repeated_payload.Add()->set_text("EEEEEEEE");

    TcpHealthCheckMatcher::MatchSegments segments =
        TcpHealthCheckMatcher::loadProtoBytes(repeated_payload);
    EXPECT_EQ(2U, segments.size());
  }

  {
    Protobuf::RepeatedPtrField<envoy::api::v2::HealthCheck::Payload> repeated_payload;
    repeated_payload.Add()->set_text("4");

    EXPECT_THROW(TcpHealthCheckMatcher::loadProtoBytes(repeated_payload), EnvoyException);
  }

  {
    Protobuf::RepeatedPtrField<envoy::api::v2::HealthCheck::Payload> repeated_payload;
    repeated_payload.Add()->set_text("gg");

    EXPECT_THROW(TcpHealthCheckMatcher::loadProtoBytes(repeated_payload), EnvoyException);
  }
}

static void add_uint8(Buffer::Instance& buffer, uint8_t addend) {
  buffer.add(&addend, sizeof(addend));
}

TEST(TcpHealthCheckMatcher, match) {
  Protobuf::RepeatedPtrField<envoy::api::v2::HealthCheck::Payload> repeated_payload;
  repeated_payload.Add()->set_text("01");
  repeated_payload.Add()->set_text("02");

  TcpHealthCheckMatcher::MatchSegments segments =
      TcpHealthCheckMatcher::loadProtoBytes(repeated_payload);

  Buffer::OwnedImpl buffer;
  EXPECT_FALSE(TcpHealthCheckMatcher::match(segments, buffer));
  add_uint8(buffer, 1);
  EXPECT_FALSE(TcpHealthCheckMatcher::match(segments, buffer));
  add_uint8(buffer, 2);
  EXPECT_TRUE(TcpHealthCheckMatcher::match(segments, buffer));

  buffer.drain(2);
  add_uint8(buffer, 1);
  add_uint8(buffer, 3);
  add_uint8(buffer, 2);
  EXPECT_TRUE(TcpHealthCheckMatcher::match(segments, buffer));

  buffer.drain(3);
  add_uint8(buffer, 0);
  add_uint8(buffer, 3);
  add_uint8(buffer, 1);
  add_uint8(buffer, 2);
  EXPECT_TRUE(TcpHealthCheckMatcher::match(segments, buffer));
}

class TcpHealthCheckerImplTest : public testing::Test {
public:
  TcpHealthCheckerImplTest() : cluster_(new NiceMock<MockCluster>()) {}

  void setupData() {
    std::string json = R"EOF(
    {
      "type": "tcp",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "unhealthy_threshold": 2,
      "healthy_threshold": 2,
      "send": [
        {"binary": "01"}
      ],
      "receive": [
        {"binary": "02"}
      ]
    }
    )EOF";

    health_checker_.reset(new TcpHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                   dispatcher_, runtime_, random_));
  }

  void setupNoData() {
    std::string json = R"EOF(
    {
      "type": "tcp",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "unhealthy_threshold": 2,
      "healthy_threshold": 2,
      "send": [],
      "receive": []
    }
    )EOF";

    health_checker_.reset(new TcpHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                   dispatcher_, runtime_, random_));
  }

  void setupDataDontReuseConnection() {
    std::string json = R"EOF(
      {
        "type": "tcp",
        "timeout_ms": 1000,
        "interval_ms": 1000,
        "unhealthy_threshold": 2,
        "healthy_threshold": 2,
        "reuse_connection": false,
        "send": [
          {"binary": "01"}
        ],
        "receive": [
          {"binary": "02"}
        ]
      }
      )EOF";

    health_checker_.reset(new TcpHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                   dispatcher_, runtime_, random_));
  }

  void expectSessionCreate() {
    interval_timer_ = new Event::MockTimer(&dispatcher_);
    timeout_timer_ = new Event::MockTimer(&dispatcher_);
  }

  void expectClientCreate() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _)).WillOnce(Return(connection_));
    EXPECT_CALL(*connection_, addReadFilter(_)).WillOnce(SaveArg<0>(&read_filter_));
  }

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::shared_ptr<TcpHealthCheckerImpl> health_checker_;
  Network::MockClientConnection* connection_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Network::ReadFilterSharedPtr read_filter_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(TcpHealthCheckerImplTest, Success) {
  InSequence s;

  setupData();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response);
}

// Tests that a successful healthcheck will disconnect the client when reuse_connection is false.
TEST_F(TcpHealthCheckerImplTest, DataWithoutReusingConnection) {
  InSequence s;

  setupDataDontReuseConnection();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(1);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected execution flow when a healthcheck is successful and reuse_connection is false.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response);

  // These are the expected metric results after testing.
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
}

TEST_F(TcpHealthCheckerImplTest, Timeout) {
  InSequence s;

  setupData();
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*connection_, write(_));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  cluster_->runCallbacks({cluster_->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response;
  add_uint8(response, 1);
  read_filter_->onData(response);

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate();
  EXPECT_CALL(*connection_, write(_));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectClientCreate();
  EXPECT_CALL(*connection_, write(_));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  std::vector<HostSharedPtr> removed{cluster_->hosts_.back()};
  cluster_->hosts_.clear();
  EXPECT_CALL(*connection_, close(_));
  cluster_->runCallbacks({}, removed);
}

// Tests that when reuse_connection is false timeouts execute normally.
TEST_F(TcpHealthCheckerImplTest, TimeoutWithoutReusingConnection) {
  InSequence s;

  setupDataDontReuseConnection();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(1);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected flow when a healthcheck is successful and reuse_connection is false.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response);

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());

  // The healthcheck will run again.
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected flow when a healthcheck times out.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  // The healthcheck is not yet at the unhealthy threshold.
  EXPECT_FALSE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  // The healthcheck metric results after first timeout block.
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.failure").value());

  // The healthcheck will run again, it should be failing after this attempt.
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected flow when a healthcheck times out.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  // The healthcheck metric results after the second timeout block.
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
}

TEST_F(TcpHealthCheckerImplTest, NoData) {
  InSequence s;

  setupNoData();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();
}

TEST_F(TcpHealthCheckerImplTest, PassiveFailure) {
  InSequence s;

  setupNoData();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Do multiple passive failures. This will not reset the active HC timers.
  cluster_->hosts_[0]->healthChecker().setUnhealthy();
  cluster_->hosts_[0]->healthChecker().setUnhealthy();
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  // A single success should not bring us back to healthy.
  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_TRUE(cluster_->hosts_[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.passive_failure").value());
}

TEST_F(TcpHealthCheckerImplTest, PassiveFailureCrossThreadRemoveHostRace) {
  InSequence s;

  setupNoData();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Do a passive failure. This will not reset the active HC timers.
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  cluster_->hosts_[0]->healthChecker().setUnhealthy();

  // Remove before the cross thread event comes in.
  EXPECT_CALL(*connection_, close(_));
  std::vector<HostSharedPtr> old_hosts = std::move(cluster_->hosts_);
  cluster_->runCallbacks({}, old_hosts);
  post_cb();

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.passive_failure").value());
}

TEST_F(TcpHealthCheckerImplTest, PassiveFailureCrossThreadRemoveClusterRace) {
  InSequence s;

  setupNoData();
  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Do a passive failure. This will not reset the active HC timers.
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  cluster_->hosts_[0]->healthChecker().setUnhealthy();

  // Remove before the cross thread event comes in.
  EXPECT_CALL(*connection_, close(_));
  health_checker_.reset();
  post_cb();

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.passive_failure").value());
}

class RedisHealthCheckerImplTest : public testing::Test, public Redis::ConnPool::ClientFactory {
public:
  RedisHealthCheckerImplTest() : cluster_(new NiceMock<MockCluster>()) {}

  void setup() {
    std::string json = R"EOF(
    {
      "type": "redis",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "unhealthy_threshold": 1,
      "healthy_threshold": 1
    }
    )EOF";

    health_checker_.reset(new RedisHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                     dispatcher_, runtime_, random_, *this));
  }

  void setupDontReuseConnection() {
    std::string json = R"EOF(
      {
        "type": "redis",
        "timeout_ms": 1000,
        "interval_ms": 1000,
        "unhealthy_threshold": 1,
        "healthy_threshold": 1,
        "reuse_connection": false
      }
      )EOF";

    health_checker_.reset(new RedisHealthCheckerImpl(*cluster_, parseHealthCheckFromJson(json),
                                                     dispatcher_, runtime_, random_, *this));
  }

  Redis::ConnPool::ClientPtr create(Upstream::HostConstSharedPtr, Event::Dispatcher&,
                                    const Redis::ConnPool::Config&) override {
    return Redis::ConnPool::ClientPtr{create_()};
  }

  MOCK_METHOD0(create_, Redis::ConnPool::Client*());

  void expectSessionCreate() {
    interval_timer_ = new Event::MockTimer(&dispatcher_);
    timeout_timer_ = new Event::MockTimer(&dispatcher_);
  }

  void expectClientCreate() {
    client_ = new Redis::ConnPool::MockClient();
    EXPECT_CALL(*this, create_()).WillOnce(Return(client_));
    EXPECT_CALL(*client_, addConnectionCallbacks(_));
  }

  void expectRequestCreate() {
    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthCheckerImpl::healthCheckRequest()), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    EXPECT_CALL(*timeout_timer_, enableTimer(_));
  }

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Redis::ConnPool::MockClient* client_{};
  Redis::ConnPool::MockPoolRequest pool_request_;
  Redis::ConnPool::PoolCallbacks* pool_callbacks_{};
  std::shared_ptr<RedisHealthCheckerImpl> health_checker_;
};

TEST_F(RedisHealthCheckerImplTest, All) {
  InSequence s;
  setup();

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectRequestCreate();
  health_checker_->start();

  // Success
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Redis::RespValuePtr response(new Redis::RespValue());
  response->type(Redis::RespType::SimpleString);
  response->asString() = "PONG";
  pool_callbacks_->onResponse(std::move(response));

  expectRequestCreate();
  interval_timer_->callback_();

  // Failure
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Redis::RespValue());
  pool_callbacks_->onResponse(std::move(response));

  expectRequestCreate();
  interval_timer_->callback_();

  // Redis failure via disconnect
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  pool_callbacks_->onFailure();
  client_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate();
  expectRequestCreate();
  interval_timer_->callback_();

  // Timeout
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();

  expectClientCreate();
  expectRequestCreate();
  interval_timer_->callback_();

  // Shutdown with active request.
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());

  EXPECT_EQ(5UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.network_failure").value());
}

// Tests that redis client will behave appropriately when reuse_connection is false.
TEST_F(RedisHealthCheckerImplTest, AllDontReuseConnection) {
  InSequence s;
  setupDontReuseConnection();

  cluster_->hosts_ = {makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectRequestCreate();
  health_checker_->start();

  // The connection will close on success.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*client_, close());
  Redis::RespValuePtr response(new Redis::RespValue());
  response->type(Redis::RespType::SimpleString);
  response->asString() = "PONG";
  pool_callbacks_->onResponse(std::move(response));

  expectClientCreate();
  expectRequestCreate();
  interval_timer_->callback_();

  // The connection will close on failure.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*client_, close());
  response.reset(new Redis::RespValue());
  pool_callbacks_->onResponse(std::move(response));

  expectClientCreate();
  expectRequestCreate();
  interval_timer_->callback_();

  // Redis failure via disconnect, the connection was closed by the other end.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  pool_callbacks_->onFailure();
  client_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate();
  expectRequestCreate();
  interval_timer_->callback_();

  // Timeout, the connection will be closed.
  EXPECT_CALL(pool_request_, cancel());
  EXPECT_CALL(*client_, close());
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();

  expectClientCreate();
  expectRequestCreate();
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

} // namespace
} // namespace Upstream
} // namespace Envoy
