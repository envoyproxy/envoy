#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/config/cds_json.h"
#include "common/grpc/common.h"
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
using testing::InvokeWithoutArgs;
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

envoy::api::v2::core::HealthCheck parseHealthCheckFromJson(const std::string& json_string) {
  envoy::api::v2::core::HealthCheck health_check;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::CdsJson::translateHealthCheck(*json_object_ptr, health_check);
  return health_check;
}

envoy::api::v2::core::HealthCheck parseHealthCheckFromYaml(const std::string& yaml_string) {
  envoy::api::v2::core::HealthCheck health_check;
  MessageUtil::loadFromYaml(yaml_string, health_check);
  return health_check;
}

envoy::api::v2::core::HealthCheck createGrpcHealthCheckConfig() {
  envoy::api::v2::core::HealthCheck health_check;
  health_check.mutable_timeout()->set_seconds(1);
  health_check.mutable_interval()->set_seconds(1);
  health_check.mutable_unhealthy_threshold()->set_value(2);
  health_check.mutable_healthy_threshold()->set_value(2);
  health_check.mutable_grpc_health_check();
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
  envoy::api::v2::core::HealthCheck health_check;
  // No health checker type set
  EXPECT_THROW(HealthCheckerFactory::create(health_check, cluster, runtime, random, dispatcher),
               EnvoyException);
  health_check.mutable_http_health_check();
  // No timeout field set.
  EXPECT_THROW(HealthCheckerFactory::create(health_check, cluster, runtime, random, dispatcher),
               MissingFieldException);
}

TEST(HealthCheckerFactoryTest, GrpcHealthCheckHTTP2NotConfiguredException) {
  NiceMock<Upstream::MockCluster> cluster;
  EXPECT_CALL(*cluster.info_, features()).WillRepeatedly(Return(0));

  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;

  EXPECT_THROW_WITH_MESSAGE(HealthCheckerFactory::create(createGrpcHealthCheckConfig(), cluster,
                                                         runtime, random, dispatcher),
                            EnvoyException,
                            "fake_cluster cluster must support HTTP/2 for gRPC healthchecking");
}

TEST(HealthCheckerFactoryTest, createGrpc) {

  NiceMock<Upstream::MockCluster> cluster;
  EXPECT_CALL(*cluster.info_, features())
      .WillRepeatedly(Return(Upstream::ClusterInfo::Features::HTTP2));

  Runtime::MockLoader runtime;
  Runtime::MockRandomGenerator random;
  Event::MockDispatcher dispatcher;

  EXPECT_NE(nullptr, dynamic_cast<GrpcHealthCheckerImpl*>(
                         HealthCheckerFactory::create(createGrpcHealthCheckConfig(), cluster,
                                                      runtime, random, dispatcher)
                             .get()));
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

  void setupServiceValidationWithCustomHostValueHC(const std::string& host) {
    std::string yaml = fmt::format(R"EOF(
    timeout: 1s
    interval: 1s
    interval_jitter: 1s
    unhealthy_threshold: 2
    healthy_threshold: 2
    http_health_check:
      service_name: locations
      path: /healthcheck
      host: {0}
    )EOF",
                                   host);

    health_checker_.reset(new TestHttpHealthCheckerImpl(*cluster_, parseHealthCheckFromYaml(yaml),
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
    test_session.codec_ = new NiceMock<Http::MockClientConnection>();
    test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
    connection_index_.push_back(index);
    codec_index_.push_back(index);

    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .Times(testing::AnyNumber())
        .WillRepeatedly(InvokeWithoutArgs([&]() -> Network::ClientConnection* {
          uint32_t index = connection_index_.front();
          connection_index_.pop_front();
          return test_sessions_[index]->client_connection_;
        }));
    EXPECT_CALL(*health_checker_, createCodecClient_(_))
        .WillRepeatedly(
            Invoke([&](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
              uint32_t index = codec_index_.front();
              codec_index_.pop_front();
              TestSession& test_session = *test_sessions_[index];
              return new CodecClientForTest(std::move(conn_data.connection_), test_session.codec_,
                                            nullptr, nullptr);
            }));
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
  std::list<uint32_t> connection_index_{};
  std::list<uint32_t> codec_index_{};
};

TEST_F(HttpHealthCheckerImplTest, Success) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessWithSpurious100Continue) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
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

  std::unique_ptr<Http::TestHeaderMapImpl> continue_headers(
      new Http::TestHeaderMapImpl{{":status", "100"}});
  test_sessions_[0]->stream_response_callbacks_->decode100ContinueHeaders(
      std::move(continue_headers));

  respond(0, "200", false, true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Test host check success with multiple hosts.
TEST_F(HttpHealthCheckerImplTest, SuccessWithMultipleHosts) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80"),
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectSessionCreate();
  expectStreamCreate(1);
  EXPECT_CALL(*test_sessions_[1]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _)).Times(2);
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .Times(2)
      .WillRepeatedly(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  EXPECT_CALL(*test_sessions_[1]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[1]->timeout_timer_, disableTimer());
  respond(0, "200", false, true);
  respond(1, "200", false, true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[1]->healthy());
}

// Test host check success with multiple hosts across multiple priorities.
TEST_F(HttpHealthCheckerImplTest, SuccessWithMultipleHostSets) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(1)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectSessionCreate();
  expectStreamCreate(1);
  EXPECT_CALL(*test_sessions_[1]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _)).Times(2);
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .Times(2)
      .WillRepeatedly(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  EXPECT_CALL(*test_sessions_[1]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[1]->timeout_timer_, disableTimer());
  respond(0, "200", false, true);
  respond(1, "200", false, true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(1)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessServiceCheck) {
  const std::string host = "fake_cluster";
  const std::string path = "/healthcheck";
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const Http::HeaderMap& headers, bool) {
        EXPECT_TRUE(headers.Host());
        EXPECT_TRUE(headers.Path());
        EXPECT_NE(nullptr, headers.Host());
        EXPECT_NE(nullptr, headers.Path());
        EXPECT_EQ(headers.Host()->value().c_str(), host);
        EXPECT_EQ(headers.Path()->value().c_str(), path);
      }));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  Optional<std::string> health_checked_cluster("locations-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessServiceCheckWithCustomHostValue) {
  const std::string host = "www.envoyproxy.io";
  const std::string path = "/healthcheck";
  setupServiceValidationWithCustomHostValueHC(host);
  // requires non-empty `service_name` in config.
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const Http::HeaderMap& headers, bool) {
        EXPECT_TRUE(headers.Host());
        EXPECT_TRUE(headers.Path());
        EXPECT_NE(nullptr, headers.Host());
        EXPECT_NE(nullptr, headers.Path());
        EXPECT_EQ(headers.Host()->value().c_str(), host);
        EXPECT_EQ(headers.Path()->value().c_str(), path);
      }));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  Optional<std::string> health_checked_cluster("locations-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceDoesNotMatchFail) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceNotPresentInResponseFail) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceCheckRuntimeOff) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(false));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedFailFirstServiceCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillRepeatedly(Return(true));
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessNoTraffic) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(60000)));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false, true, true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedSuccessFirst) {
  setupNoServiceValidationHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedFailFirst) {
  setupNoServiceValidationHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Test that failing first disables fast success.
  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "503", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, HttpFail) {
  setupNoServiceValidationHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "503", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, Disconnect) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(cluster_->prioritySet().getMockHostSet(0)->hosts_[0], true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, Timeout) {
  setupNoServiceValidationHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, DynamicAddAndRemove) {
  setupNoServiceValidationHC();
  health_checker_->start();

  expectSessionCreate();
  expectStreamCreate(0);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  HostVector removed{cluster_->prioritySet().getMockHostSet(0)->hosts_.back()};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);
}

TEST_F(HttpHealthCheckerImplTest, ConnectionClose) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();
}

TEST_F(HttpHealthCheckerImplTest, RemoteCloseBetweenChecks) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate(0);
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Test that we close connections on a healthy check when reuse_connection is false.
TEST_F(HttpHealthCheckerImplTest, DontReuseConnectionBetweenChecks) {
  setupNoServiceValidationNoReuseConnectionHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

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
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, StreamReachesWatermarkDuringCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());

  test_sessions_[0]->request_encoder_.stream_.runHighWatermarkCallbacks();
  test_sessions_[0]->request_encoder_.stream_.runLowWatermarkCallbacks();

  respond(0, "200", true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ConnectionReachesWatermarkDuringCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectStreamCreate(0);
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, disableTimer());

  test_sessions_[0]->client_connection_->runHighWatermarkCallbacks();
  test_sessions_[0]->client_connection_->runLowWatermarkCallbacks();

  respond(0, "200", true);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

TEST(TcpHealthCheckMatcher, loadJsonBytes) {
  {
    Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload> repeated_payload;
    repeated_payload.Add()->set_text("39000000");
    repeated_payload.Add()->set_text("EEEEEEEE");

    TcpHealthCheckMatcher::MatchSegments segments =
        TcpHealthCheckMatcher::loadProtoBytes(repeated_payload);
    EXPECT_EQ(2U, segments.size());
  }

  {
    Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload> repeated_payload;
    repeated_payload.Add()->set_text("4");

    EXPECT_THROW(TcpHealthCheckMatcher::loadProtoBytes(repeated_payload), EnvoyException);
  }

  {
    Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload> repeated_payload;
    repeated_payload.Add()->set_text("gg");

    EXPECT_THROW(TcpHealthCheckMatcher::loadProtoBytes(repeated_payload), EnvoyException);
  }
}

static void add_uint8(Buffer::Instance& buffer, uint8_t addend) {
  buffer.add(&addend, sizeof(addend));
}

TEST(TcpHealthCheckMatcher, match) {
  Protobuf::RepeatedPtrField<envoy::api::v2::core::HealthCheck::Payload> repeated_payload;
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
    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _)).WillOnce(Return(connection_));
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
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response, false);
}

// Tests that a successful healthcheck will disconnect the client when reuse_connection is false.
TEST_F(TcpHealthCheckerImplTest, DataWithoutReusingConnection) {
  InSequence s;

  setupDataDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(1);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected execution flow when a healthcheck is successful and reuse_connection is false.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response, false);

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
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*connection_, write(_, _));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  Buffer::OwnedImpl response;
  add_uint8(response, 1);
  read_filter_->onData(response, false);

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  HostVector removed{cluster_->prioritySet().getMockHostSet(0)->hosts_.back()};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  EXPECT_CALL(*connection_, close(_));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);
}

// Tests that when reuse_connection is false timeouts execute normally.
TEST_F(TcpHealthCheckerImplTest, TimeoutWithoutReusingConnection) {
  InSequence s;

  setupDataDontReuseConnection();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(1);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected flow when a healthcheck is successful and reuse_connection is false.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);

  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response, false);

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());

  // The healthcheck will run again.
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected flow when a healthcheck times out.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  // The healthcheck is not yet at the unhealthy threshold.
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  // The healthcheck metric results after first timeout block.
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.failure").value());

  // The healthcheck will run again, it should be failing after this attempt.
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _));
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();

  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  // Expected flow when a healthcheck times out.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  // The healthcheck metric results after the second timeout block.
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
}

TEST_F(TcpHealthCheckerImplTest, NoData) {
  InSequence s;

  setupNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);

  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  interval_timer_->callback_();
}

TEST_F(TcpHealthCheckerImplTest, PassiveFailure) {
  InSequence s;

  setupNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Do multiple passive failures. This will not reset the active HC timers.
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthChecker().setUnhealthy();
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthChecker().setUnhealthy();
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  // A single success should not bring us back to healthy.
  EXPECT_CALL(*connection_, close(_));
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvent(Network::ConnectionEvent::Connected);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.passive_failure").value());
}

TEST_F(TcpHealthCheckerImplTest, PassiveFailureCrossThreadRemoveHostRace) {
  InSequence s;

  setupNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Do a passive failure. This will not reset the active HC timers.
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthChecker().setUnhealthy();

  // Remove before the cross thread event comes in.
  EXPECT_CALL(*connection_, close(_));
  HostVector old_hosts = std::move(cluster_->prioritySet().getMockHostSet(0)->hosts_);
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, old_hosts);
  post_cb();

  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
  EXPECT_EQ(0UL, cluster_->info_->stats_store_.counter("health_check.passive_failure").value());
}

TEST_F(TcpHealthCheckerImplTest, PassiveFailureCrossThreadRemoveClusterRace) {
  InSequence s;

  setupNoData();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();
  expectClientCreate();
  EXPECT_CALL(*connection_, write(_, _)).Times(0);
  EXPECT_CALL(*timeout_timer_, enableTimer(_));
  health_checker_->start();

  // Do a passive failure. This will not reset the active HC timers.
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthChecker().setUnhealthy();

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

  void setupExistsHealthcheck() {
    std::string json = R"EOF(
    {
      "type": "redis",
      "timeout_ms": 1000,
      "interval_ms": 1000,
      "unhealthy_threshold": 1,
      "healthy_threshold": 1,
      "redis_key": "foo"
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

  void expectExistsRequestCreate() {
    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthCheckerImpl::existsHealthCheckRequest("")), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&pool_callbacks_)), Return(&pool_request_)));
    EXPECT_CALL(*timeout_timer_, enableTimer(_));
  }

  void expectPingRequestCreate() {
    EXPECT_CALL(*client_, makeRequest(Ref(RedisHealthCheckerImpl::pingHealthCheckRequest()), _))
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

TEST_F(RedisHealthCheckerImplTest, PingAndVariousFailures) {
  InSequence s;
  setup();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectPingRequestCreate();
  health_checker_->start();

  client_->runHighWatermarkCallbacks();
  client_->runLowWatermarkCallbacks();

  // Success
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Redis::RespValuePtr response(new Redis::RespValue());
  response->type(Redis::RespType::SimpleString);
  response->asString() = "PONG";
  pool_callbacks_->onResponse(std::move(response));

  expectPingRequestCreate();
  interval_timer_->callback_();

  // Failure
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Redis::RespValue());
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

TEST_F(RedisHealthCheckerImplTest, Exists) {
  InSequence s;
  setupExistsHealthcheck();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectExistsRequestCreate();
  health_checker_->start();

  client_->runHighWatermarkCallbacks();
  client_->runLowWatermarkCallbacks();

  // Success
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Redis::RespValuePtr response(new Redis::RespValue());
  response->type(Redis::RespType::Integer);
  response->asInteger() = 0;
  pool_callbacks_->onResponse(std::move(response));

  expectExistsRequestCreate();
  interval_timer_->callback_();

  // Failure, exists
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Redis::RespValue());
  response->type(Redis::RespType::Integer);
  response->asInteger() = 1;
  pool_callbacks_->onResponse(std::move(response));

  expectExistsRequestCreate();
  interval_timer_->callback_();

  // Failure, no value
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  response.reset(new Redis::RespValue());
  pool_callbacks_->onResponse(std::move(response));

  EXPECT_CALL(*client_, close());

  EXPECT_EQ(3UL, cluster_->info_->stats_store_.counter("health_check.attempt").value());
  EXPECT_EQ(1UL, cluster_->info_->stats_store_.counter("health_check.success").value());
  EXPECT_EQ(2UL, cluster_->info_->stats_store_.counter("health_check.failure").value());
}

// Tests that redis client will behave appropriately when reuse_connection is false.
TEST_F(RedisHealthCheckerImplTest, NoConnectionReuse) {
  InSequence s;
  setupDontReuseConnection();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectClientCreate();
  expectPingRequestCreate();
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
  expectPingRequestCreate();
  interval_timer_->callback_();

  // The connection will close on failure.
  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*client_, close());
  response.reset(new Redis::RespValue());
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

class TestGrpcHealthCheckerImpl : public GrpcHealthCheckerImpl {
public:
  using GrpcHealthCheckerImpl::GrpcHealthCheckerImpl;

  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& conn_data) override {
    auto codec_client = createCodecClient_(conn_data);
    return Http::CodecClientPtr(codec_client);
  };

  // GrpcHealthCheckerImpl
  MOCK_METHOD1(createCodecClient_, Http::CodecClient*(Upstream::Host::CreateConnectionData&));
};

class GrpcHealthCheckerImplTestBase {
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
    CodecClientForTest* codec_client_{};
  };

  typedef std::unique_ptr<TestSession> TestSessionPtr;

  struct ResponseSpec {
    struct ChunkSpec {
      bool valid;
      std::vector<uint8_t> data;
    };
    static ChunkSpec invalidChunk() {
      ChunkSpec spec;
      spec.valid = false;
      return spec;
    }
    static ChunkSpec invalidPayload(uint8_t flags, bool valid_message) {
      ChunkSpec spec;
      spec.valid = true;
      spec.data = serializeResponse(grpc::health::v1::HealthCheckResponse::SERVING);
      spec.data[0] = flags;
      if (!valid_message) {
        const size_t kGrpcHeaderSize = 5;
        for (size_t i = kGrpcHeaderSize; i < spec.data.size(); i++) {
          // Fill payload with some random data.
          spec.data[i] = i % 256;
        }
      }
      return spec;
    }
    static ChunkSpec validChunk(grpc::health::v1::HealthCheckResponse::ServingStatus status) {
      ChunkSpec spec;
      spec.valid = true;
      spec.data = serializeResponse(status);
      return spec;
    }

    static ChunkSpec servingResponse() {
      return validChunk(grpc::health::v1::HealthCheckResponse::SERVING);
    }

    static ChunkSpec notServingResponse() {
      return validChunk(grpc::health::v1::HealthCheckResponse::NOT_SERVING);
    }

    static std::vector<uint8_t>
    serializeResponse(grpc::health::v1::HealthCheckResponse::ServingStatus status) {
      grpc::health::v1::HealthCheckResponse response;
      response.set_status(status);
      const auto data = Grpc::Common::serializeBody(response);
      auto ret = std::vector<uint8_t>(data->length(), 0);
      data->copyOut(0, data->length(), &ret[0]);
      return ret;
    }

    std::vector<std::pair<std::string, std::string>> response_headers;
    std::vector<ChunkSpec> body_chunks;
    std::vector<std::pair<std::string, std::string>> trailers;
  };

  GrpcHealthCheckerImplTestBase() : cluster_(new NiceMock<MockCluster>()) {
    EXPECT_CALL(*cluster_->info_, features())
        .WillRepeatedly(Return(Upstream::ClusterInfo::Features::HTTP2));
  }

  void setupHC() {
    const auto config = createGrpcHealthCheckConfig();
    health_checker_.reset(
        new TestGrpcHealthCheckerImpl(*cluster_, config, dispatcher_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr host, bool changed_state) -> void {
      onHostStatus(host, changed_state);
    });
  }

  void setupHCWithUnhealthyThreshold(int value) {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_unhealthy_threshold()->set_value(value);
    health_checker_.reset(
        new TestGrpcHealthCheckerImpl(*cluster_, config, dispatcher_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr host, bool changed_state) -> void {
      onHostStatus(host, changed_state);
    });
  }

  void setupServiceNameHC() {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_grpc_health_check()->set_service_name("service");
    health_checker_.reset(
        new TestGrpcHealthCheckerImpl(*cluster_, config, dispatcher_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr host, bool changed_state) -> void {
      onHostStatus(host, changed_state);
    });
  }

  void setupNoReuseConnectionHC() {
    auto config = createGrpcHealthCheckConfig();
    config.mutable_reuse_connection()->set_value(false);
    health_checker_.reset(
        new TestGrpcHealthCheckerImpl(*cluster_, config, dispatcher_, runtime_, random_));
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
    test_session.codec_ = new NiceMock<Http::MockClientConnection>();
    test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();
    connection_index_.push_back(index);
    codec_index_.push_back(index);

    EXPECT_CALL(dispatcher_, createClientConnection_(_, _, _, _))
        .Times(testing::AnyNumber())
        .WillRepeatedly(InvokeWithoutArgs([&]() -> Network::ClientConnection* {
          uint32_t index = connection_index_.front();
          connection_index_.pop_front();
          return test_sessions_[index]->client_connection_;
        }));
    EXPECT_CALL(*health_checker_, createCodecClient_(_))
        .WillRepeatedly(
            Invoke([&](Upstream::Host::CreateConnectionData& conn_data) -> Http::CodecClient* {
              uint32_t index = codec_index_.front();
              codec_index_.pop_front();
              TestSession& test_session = *test_sessions_[index];
              test_session.codec_client_ = new CodecClientForTest(
                  std::move(conn_data.connection_), test_session.codec_, nullptr, nullptr);
              return test_session.codec_client_;
            }));
  }

  void expectStreamCreate(size_t index) {
    test_sessions_[index]->request_encoder_.stream_.callbacks_.clear();
    EXPECT_CALL(*test_sessions_[index]->codec_, newStream(_))
        .WillOnce(DoAll(SaveArgAddress(&test_sessions_[index]->stream_response_callbacks_),
                        ReturnRef(test_sessions_[index]->request_encoder_)));
  }

  // Starts healthchecker and sets up timer expectations, leaving up future specification of
  // healthcheck response for the caller. Useful when there is only one healthcheck attempt
  // performed during test case (but possibly on many hosts).
  void expectHealthchecks(bool host_state_changed, size_t num_healthchecks) {
    for (size_t i = 0; i < num_healthchecks; i++) {
      cluster_->info_->stats().upstream_cx_total_.inc();
      expectSessionCreate();
      expectHealthcheckStart(i);
    }
    health_checker_->start();

    EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _))
        .Times(num_healthchecks);
    EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
        .Times(num_healthchecks)
        .WillRepeatedly(Return(45000));
    for (size_t i = 0; i < num_healthchecks; i++) {
      expectHealthcheckStop(i, 45000);
    }
    EXPECT_CALL(*this, onHostStatus(_, host_state_changed)).Times(num_healthchecks);
  }

  void expectSingleHealthcheck(bool host_state_changed) {
    cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
        makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
    expectHealthchecks(host_state_changed, 1);
  }

  // Hides timer/stream-related boilerplate of healthcheck start.
  void expectHealthcheckStart(size_t index) {
    expectStreamCreate(index);
    EXPECT_CALL(*test_sessions_[index]->timeout_timer_, enableTimer(_));
  }

  // Hides timer-related boilerplate of healthcheck stop.
  void expectHealthcheckStop(size_t index, int interval_ms = 0) {
    if (interval_ms > 0) {
      EXPECT_CALL(*test_sessions_[index]->interval_timer_,
                  enableTimer(std::chrono::milliseconds(interval_ms)));
    } else {
      EXPECT_CALL(*test_sessions_[index]->interval_timer_, enableTimer(_));
    }
    EXPECT_CALL(*test_sessions_[index]->timeout_timer_, disableTimer());
  }

  // Hides host status checking boilerplate when only single host is used in test.
  void expectHostHealthy(bool healthy) {
    const auto host = cluster_->prioritySet().getMockHostSet(0)->hosts_[0];
    if (!healthy) {
      EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      EXPECT_FALSE(host->healthy());
    } else {
      EXPECT_TRUE(host->healthy());
    }
  }

  void respondServiceStatus(size_t index,
                            grpc::health::v1::HealthCheckResponse::ServingStatus status) {
    respondResponseSpec(index,
                        ResponseSpec{{{":status", "200"}, {"content-type", "application/grpc"}},
                                     {ResponseSpec::validChunk(status)},
                                     {{"grpc-status", "0"}}});
  }

  void respondResponseSpec(size_t index, ResponseSpec&& spec) {
    const bool trailers_empty = spec.trailers.size() == 0U;
    const bool end_stream_on_headers = spec.body_chunks.empty() && trailers_empty;
    auto response_headers = std::make_unique<Http::TestHeaderMapImpl>();
    for (const auto& header : spec.response_headers) {
      response_headers->addCopy(header.first, header.second);
    }
    test_sessions_[index]->stream_response_callbacks_->decodeHeaders(std::move(response_headers),
                                                                     end_stream_on_headers);
    for (size_t i = 0; i < spec.body_chunks.size(); i++) {
      const bool end_stream = i == spec.body_chunks.size() - 1 && trailers_empty;
      const auto& chunk = spec.body_chunks[i];
      if (chunk.valid) {
        const auto data = std::make_unique<Buffer::OwnedImpl>(chunk.data.data(), chunk.data.size());
        test_sessions_[index]->stream_response_callbacks_->decodeData(*data, end_stream);
      } else {
        Buffer::OwnedImpl incorrect_data("incorrect");
        test_sessions_[index]->stream_response_callbacks_->decodeData(incorrect_data, end_stream);
      }
    }
    if (!trailers_empty) {
      auto trailers = std::make_unique<Http::TestHeaderMapImpl>();
      for (const auto& header : spec.trailers) {
        trailers->addCopy(header.first, header.second);
      }
      test_sessions_[index]->stream_response_callbacks_->decodeTrailers(std::move(trailers));
    }
  }

  MOCK_METHOD2(onHostStatus, void(HostSharedPtr host, bool changed_state));

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::vector<TestSessionPtr> test_sessions_;
  std::shared_ptr<TestGrpcHealthCheckerImpl> health_checker_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::list<uint32_t> connection_index_{};
  std::list<uint32_t> codec_index_{};
};

class GrpcHealthCheckerImplTest : public GrpcHealthCheckerImplTestBase, public testing::Test {};

// Test single host check success.
TEST_F(GrpcHealthCheckerImplTest, Success) {
  setupServiceNameHC();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->info_->stats().upstream_cx_total_.inc();

  expectSessionCreate();
  expectHealthcheckStart(0);

  EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const Http::HeaderMap& headers, bool) {
        EXPECT_EQ(Http::Headers::get().ContentTypeValues.Grpc,
                  headers.ContentType()->value().c_str());
        EXPECT_EQ(std::string("/grpc.health.v1.Health/Check"), headers.Path()->value().c_str());
        EXPECT_EQ(Http::Headers::get().SchemeValues.Http, headers.Scheme()->value().c_str());
        EXPECT_NE(nullptr, headers.Method());
        EXPECT_NE(nullptr, headers.Host());
      }));
  EXPECT_CALL(test_sessions_[0]->request_encoder_, encodeData(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        std::vector<Grpc::Frame> decoded_frames;
        Grpc::Decoder decoder;
        ASSERT_TRUE(decoder.decode(data, decoded_frames));
        ASSERT_EQ(1U, decoded_frames.size());
        auto& frame = decoded_frames[0];
        Buffer::ZeroCopyInputStreamImpl stream(std::move(frame.data_));
        grpc::health::v1::HealthCheckRequest request;
        ASSERT_TRUE(request.ParseFromZeroCopyStream(&stream));
        EXPECT_EQ("service", request.service());
      }));
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  expectHealthcheckStop(0, 45000);

  // Host state should not be changed (remains healty).
  EXPECT_CALL(*this, onHostStatus(cluster_->prioritySet().getMockHostSet(0)->hosts_[0], false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test host check success when gRPC response payload is split between several incoming data chunks.
TEST_F(GrpcHealthCheckerImplTest, SuccessResponseSplitBetweenChunks) {
  setupServiceNameHC();
  expectSingleHealthcheck(false);

  auto response_headers = std::make_unique<Http::TestHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{
          {":status", "200"},
          {"content-type", "application/grpc"},
      });
  test_sessions_[0]->stream_response_callbacks_->decodeHeaders(std::move(response_headers), false);

  grpc::health::v1::HealthCheckResponse response;
  response.set_status(grpc::health::v1::HealthCheckResponse::SERVING);
  auto data = Grpc::Common::serializeBody(response);

  const char* raw_data = static_cast<char*>(data->linearize(data->length()));
  const uint64_t chunk_size = data->length() / 5;
  for (uint64_t offset = 0; offset < data->length(); offset += chunk_size) {
    const uint64_t effective_size = std::min(chunk_size, data->length() - offset);
    const auto chunk = std::make_unique<Buffer::OwnedImpl>(raw_data + offset, effective_size);
    test_sessions_[0]->stream_response_callbacks_->decodeData(*chunk, false);
  }

  auto trailers = std::make_unique<Http::TestHeaderMapImpl>(
      std::initializer_list<std::pair<std::string, std::string>>{{"grpc-status", "0"}});
  test_sessions_[0]->stream_response_callbacks_->decodeTrailers(std::move(trailers));

  expectHostHealthy(true);
}

// Test host check success with multiple hosts.
TEST_F(GrpcHealthCheckerImplTest, SuccessWithMultipleHosts) {
  setupHC();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80"),
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};

  expectHealthchecks(false, 2);

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  respondServiceStatus(1, grpc::health::v1::HealthCheckResponse::SERVING);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[1]->healthy());
}

// Test host check success with multiple hosts across multiple priorities.
TEST_F(GrpcHealthCheckerImplTest, SuccessWithMultipleHostSets) {
  setupHC();

  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(1)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:81")};

  expectHealthchecks(false, 2);

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  respondServiceStatus(1, grpc::health::v1::HealthCheckResponse::SERVING);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(1)->hosts_[0]->healthy());
}

// Test stream-level watermarks does not interfere with health check.
TEST_F(GrpcHealthCheckerImplTest, StreamReachesWatermarkDuringCheck) {
  setupHC();
  expectSingleHealthcheck(false);

  test_sessions_[0]->request_encoder_.stream_.runHighWatermarkCallbacks();
  test_sessions_[0]->request_encoder_.stream_.runLowWatermarkCallbacks();

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test connection-level watermarks does not interfere with health check.
TEST_F(GrpcHealthCheckerImplTest, ConnectionReachesWatermarkDuringCheck) {
  setupHC();
  expectSingleHealthcheck(false);

  test_sessions_[0]->client_connection_->runHighWatermarkCallbacks();
  test_sessions_[0]->client_connection_->runLowWatermarkCallbacks();

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test health check on host without traffic sets larger unconfigurable interval for the next check.
TEST_F(GrpcHealthCheckerImplTest, SuccessNoTraffic) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  // Default healthcheck interval for hosts without traffic is 60 seconds.
  expectHealthcheckStop(0, 60000);
  // Host state should not be changed (remains healty).
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test first successful check immediately makes failed host available (without 2nd probe).
TEST_F(GrpcHealthCheckerImplTest, SuccessStartFailedSuccessFirst) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _)).WillOnce(Return(500));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _));
  expectHealthcheckStop(0, 500);
  // Fast success immediately moves us to healthy.
  EXPECT_CALL(*this, onHostStatus(_, true));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test host recovery after first failed check requires several successul checks.
TEST_F(GrpcHealthCheckerImplTest, SuccessStartFailedFailFirst) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  // Failing first disables fast success.
  expectHealthcheckStop(0);
  // Host was unhealthy from the start, no state change.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);
  expectHostHealthy(false);

  // Next successful healthcheck does not move host int healthy state (because we configured
  // healthchecker this way).
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Host still unhealthy, need yet another healthcheck.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);

  // 2nd successful healthcheck renders host healthy.
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, true));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test host recovery after explicit check failure requires several successul checks.
TEST_F(GrpcHealthCheckerImplTest, GrpcHealthFail) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  // Explicit healthcheck failure immediately renders host unhealthy.
  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, true));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::NOT_SERVING);
  expectHostHealthy(false);

  // Next, we need 2 successful checks for host to become available again.
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Host still considered unhealthy.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(false);

  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Host should has become healthy.
  EXPECT_CALL(*this, onHostStatus(_, true));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test disconnects produce network-type failures which does not lead to immediate unhealthy state.
TEST_F(GrpcHealthCheckerImplTest, Disconnect) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  // Network-type healthcheck failure should make host unhealthy only after 2nd event in a row.
  EXPECT_CALL(*this, onHostStatus(_, false));
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(true);

  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Now, host should be unhealthy.
  EXPECT_CALL(*this, onHostStatus(_, true));
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(false);
}

// Test timeouts produce network-type failures which does not lead to immediate unhealthy state.
TEST_F(GrpcHealthCheckerImplTest, Timeout) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  expectSessionCreate();

  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  // Timeouts are considered network failures and make host unhealthy also after 2nd event.
  EXPECT_CALL(*this, onHostStatus(_, false));
  test_sessions_[0]->timeout_timer_->callback_();
  expectHostHealthy(true);

  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, true));
  // Close connection. Timeouts and connection closes counts together.
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
  expectHostHealthy(false);
}

// Test adding and removal of hosts starts and closes healthcheck sessions.
TEST_F(GrpcHealthCheckerImplTest, DynamicAddAndRemove) {
  setupHC();
  health_checker_->start();

  expectSessionCreate();
  expectStreamCreate(0);
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};
  EXPECT_CALL(*test_sessions_[0]->timeout_timer_, enableTimer(_));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks(
      {cluster_->prioritySet().getMockHostSet(0)->hosts_.back()}, {});

  HostVector removed{cluster_->prioritySet().getMockHostSet(0)->hosts_.back()};
  cluster_->prioritySet().getMockHostSet(0)->hosts_.clear();
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  cluster_->prioritySet().getMockHostSet(0)->runCallbacks({}, removed);
}

// Test connection close between checks affects nothing.
TEST_F(GrpcHealthCheckerImplTest, RemoteCloseBetweenChecks) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // Connection closed between checks - nothing happens, just re-create client.
  test_sessions_[0]->client_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test that we close connections on a healthy check when reuse_connection is false.
TEST_F(GrpcHealthCheckerImplTest, DontReuseConnectionBetweenChecks) {
  setupNoReuseConnectionHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // A new client is created because we close the connection ourselves.
  // See GrpcHealthCheckerImplTest.RemoteCloseBetweenChecks for how this works when the remote end
  // closes the connection.
  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

// Test UNKNOWN health status is considered unhealthy.
TEST_F(GrpcHealthCheckerImplTest, GrpcFailUnknown) {
  setupHC();
  expectSingleHealthcheck(true);

  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::UNKNOWN);
  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Test receiving GOAWAY is interpreted as connection close event.
TEST_F(GrpcHealthCheckerImplTest, GoAwayProbeInProgress) {
  // FailureType::Network will be issued, it will render host unhealthy only if unhealthy_threshold
  // is reached.
  setupHCWithUnhealthyThreshold(1);
  expectSingleHealthcheck(true);

  test_sessions_[0]->codec_client_->raiseGoAway();

  EXPECT_TRUE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthFlagGet(
      Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_FALSE(cluster_->prioritySet().getMockHostSet(0)->hosts_[0]->healthy());
}

// Test receing GOAWAY between checks affects nothing.
TEST_F(GrpcHealthCheckerImplTest, GoAwayBetweenChecks) {
  setupHC();
  cluster_->prioritySet().getMockHostSet(0)->hosts_ = {
      makeTestHost(cluster_->info_, "tcp://127.0.0.1:80")};

  expectSessionCreate();
  expectHealthcheckStart(0);
  health_checker_->start();

  expectHealthcheckStop(0);
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);

  // GOAWAY between checks should go unnoticed.
  test_sessions_[0]->codec_client_->raiseGoAway();

  expectClientCreate(0);
  expectHealthcheckStart(0);
  test_sessions_[0]->interval_timer_->callback_();

  expectHealthcheckStop(0);
  // Test host state haven't changed.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respondServiceStatus(0, grpc::health::v1::HealthCheckResponse::SERVING);
  expectHostHealthy(true);
}

class BadResponseGrpcHealthCheckerImplTest
    : public GrpcHealthCheckerImplTestBase,
      public testing::TestWithParam<GrpcHealthCheckerImplTest::ResponseSpec> {};

INSTANTIATE_TEST_CASE_P(
    BadResponse, BadResponseGrpcHealthCheckerImplTest,
    testing::ValuesIn(std::vector<GrpcHealthCheckerImplTest::ResponseSpec>{
        // Non-200 response.
        {
            {{":status", "500"}},
            {},
            {},
        },
        // Non-200 response with gRPC status.
        {
            {{":status", "500"}, {"grpc-status", "2"}},
            {},
            {},
        },
        // Missing content-type.
        {
            {{":status", "200"}},
            {},
            {},
        },
        // End stream on response headers.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {},
            {},
        },
        // Non-OK gRPC status in headers.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}, {"grpc-status", "2"}},
            {},
            {},
        },
        // Non-OK gRPC status
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {{"grpc-status", "2"}},
        },
        // Missing body.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}, {"grpc-status", "0"}},
            {},
            {},
        },
        // Compressed body.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::invalidPayload(Grpc::GRPC_FH_COMPRESSED,
                                                                     true)},
            {},
        },
        // Invalid proto message.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::invalidPayload(Grpc::GRPC_FH_DEFAULT, false)},
            {},
        },
        // Duplicate response.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse(),
             GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {},
        },
        // Invalid response.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::invalidChunk()},
            {},
        },
        // No trailers.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {},
        },
        // No gRPC status in trailer.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {{"some-header", "1"}},
        },
        // Invalid gRPC status.
        {
            {{":status", "200"}, {"content-type", "application/grpc"}},
            {GrpcHealthCheckerImplTest::ResponseSpec::servingResponse()},
            {{"grpc-status", "invalid"}},
        },
    }));

// Test different cases of invalid gRPC response makes host unhealthy.
TEST_P(BadResponseGrpcHealthCheckerImplTest, GrpcBadResponse) {
  setupHC();
  expectSingleHealthcheck(true);

  ResponseSpec spec = GetParam();
  respondResponseSpec(0, std::move(spec));
  expectHostHealthy(false);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
