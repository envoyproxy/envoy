#include "common/buffer/buffer_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Upstream {

class TestHttpHealthCheckerImpl : public HttpHealthCheckerImpl {
public:
  using HttpHealthCheckerImpl::HttpHealthCheckerImpl;

  Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData&) override {
    return createCodecClient_();
  };

  // HttpHealthCheckerImpl
  MOCK_METHOD0(createCodecClient_, Http::CodecClient*());
};

class HttpHealthCheckerImplTest : public testing::Test {
public:
  struct TestSession {
    TestSession() : stats_{ALL_CODEC_CLIENT_STATS(POOL_COUNTER(stats_store_))} {}

    Event::MockTimer* interval_timer_{};
    Event::MockTimer* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Http::CodecClientStats stats_;
    Network::MockClientConnection* client_connection_{};
    Http::CodecClient* codec_client_{};
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

    Json::StringLoader config(json);
    health_checker_.reset(
        new TestHttpHealthCheckerImpl(*cluster_, config, dispatcher_, stats_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostPtr host, bool changed_state)
                                                -> void { onHostStatus(host, changed_state); });
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

    Json::StringLoader config(json);
    health_checker_.reset(
        new TestHttpHealthCheckerImpl(*cluster_, config, dispatcher_, stats_, runtime_, random_));
    health_checker_->addHostCheckCompleteCb([this](HostPtr host, bool changed_state)
                                                -> void { onHostStatus(host, changed_state); });
  }

  void expectSessionCreate() {
    // Expectations are in LIFO order.
    TestSessionPtr new_test_session(new TestSession());
    test_sessions_.emplace_back(std::move(new_test_session));
    TestSession& test_session = *test_sessions_.back();
    test_session.timeout_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    test_session.interval_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    expectClientCreate(test_sessions_.size() - 1);
  }

  void expectClientCreate(size_t index) {
    TestSession& test_session = *test_sessions_[index];

    test_session.codec_ = new NiceMock<Http::MockClientConnection>();
    test_session.client_connection_ = new NiceMock<Network::MockClientConnection>();

    Network::ClientConnectionPtr connection{test_session.client_connection_};
    test_session.codec_client_ = new CodecClientForTest(std::move(connection), test_session.codec_,
                                                        nullptr, test_session.stats_);
    EXPECT_CALL(*health_checker_, createCodecClient_())
        .WillOnce(Return(test_session.codec_client_));
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
    Http::HeaderMapPtr response_headers(new Http::HeaderMapImpl{{":status", code}});
    if (service_cluster.valid()) {
      response_headers->addViaCopy(Http::Headers::get().EnvoyUpstreamHealthCheckedCluster,
                                   service_cluster.value());
    }
    if (conn_close) {
      response_headers->addViaCopy("connection", "close");
    }

    test_sessions_[index]->stream_response_callbacks_->decodeHeaders(std::move(response_headers),
                                                                     !body && !trailers);
    if (body) {
      Buffer::OwnedImpl response_data;
      test_sessions_[index]->stream_response_callbacks_->decodeData(response_data, !trailers);
    }

    if (trailers) {
      test_sessions_[index]->stream_response_callbacks_->decodeTrailers(
          Http::HeaderMapPtr{new Http::HeaderMapImpl{{"some", "trailer"}}});
    }
  }

  MOCK_METHOD2(onHostStatus, void(HostPtr host, bool changed_state));

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_;
  std::vector<TestSessionPtr> test_sessions_;
  std::unique_ptr<TestHttpHealthCheckerImpl> health_checker_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(HttpHealthCheckerImplTest, Success) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  respond(0, "200", false, true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessServiceCheck) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  Optional<std::string> health_checked_cluster("locations-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceDoesNotMatchFail) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  Optional<std::string> health_checked_cluster("api-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceNotPresentInResponseFail) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(true));

  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  respond(0, "200", false, true, false);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, ServiceCheckRuntimeOff) {
  setupServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillOnce(Return(false));

  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->stats().upstream_cx_total_.inc();
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _))
      .WillOnce(Return(45000));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(45000)));
  Optional<std::string> health_checked_cluster("api-production-iad");
  respond(0, "200", false, true, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedFailFirstServiceCheck) {
  setupNoServiceValidationHC();
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("health_check.verify_cluster", 100))
      .WillRepeatedly(Return(true));
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->hosts_[0]->healthy(false);
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();
  Optional<std::string> health_checked_cluster("locations-production-iad");

  // Test that failing first disables fast success.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respond(0, "503", false, false, false, health_checked_cluster);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*this, onHostStatus(_, false));
  respond(0, "200", false, false, false, health_checked_cluster);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*this, onHostStatus(_, true));
  respond(0, "200", false, false, false, health_checked_cluster);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessNoTraffic) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(60000)));
  respond(0, "200", false, true, true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedSuccessFirst) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->hosts_[0]->healthy(false);
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  // Test fast success immediately moves us to healthy.
  EXPECT_CALL(*this, onHostStatus(_, true)).Times(1);
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.max_interval", _)).WillOnce(Return(500));
  EXPECT_CALL(runtime_.snapshot_, getInteger("health_check.min_interval", _));
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(std::chrono::milliseconds(500)));
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, SuccessStartFailedFailFirst) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->hosts_[0]->healthy(false);
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  // Test that failing first disables fast success.
  EXPECT_CALL(*this, onHostStatus(_, false));
  respond(0, "503", false);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*this, onHostStatus(_, false));
  respond(0, "200", false);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*this, onHostStatus(_, true));
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, HttpFail) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, true));
  respond(0, "503", false);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*this, onHostStatus(_, false));
  respond(0, "200", false);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*this, onHostStatus(_, true));
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, Disconnect) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(1);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  test_sessions_[0]->client_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(cluster_->hosts_[0], true));
  test_sessions_[0]->client_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, Timeout) {
  setupNoServiceValidationHC();
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(*this, onHostStatus(_, false));
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  test_sessions_[0]->timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();

  EXPECT_CALL(*this, onHostStatus(_, true));
  test_sessions_[0]->client_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());
}

TEST_F(HttpHealthCheckerImplTest, DynamicAddAndRemove) {
  setupNoServiceValidationHC();
  health_checker_->start();

  expectSessionCreate();
  expectStreamCreate(0);
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->runCallbacks({cluster_->hosts_.back()}, {});

  std::vector<HostPtr> removed{cluster_->hosts_.back()};
  cluster_->hosts_.clear();
  EXPECT_CALL(*test_sessions_[0]->client_connection_, close(_));
  cluster_->runCallbacks({}, removed);
}

TEST_F(HttpHealthCheckerImplTest, ConnectionClose) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false));

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  respond(0, "200", true);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate(0);
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
}

TEST_F(HttpHealthCheckerImplTest, RemoteCloseBetweenChecks) {
  setupNoServiceValidationHC();
  EXPECT_CALL(*this, onHostStatus(_, false)).Times(2);

  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectStreamCreate(0);
  health_checker_->start();

  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  test_sessions_[0]->client_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);

  expectClientCreate(0);
  expectStreamCreate(0);
  test_sessions_[0]->interval_timer_->callback_();
  EXPECT_CALL(*test_sessions_[0]->interval_timer_, enableTimer(_));
  respond(0, "200", false);
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());
}

TEST(TcpHealthCheckMatcher, loadJsonBytes) {
  {
    std::string json = R"EOF(
    {
      "bytes": [
        {"binary": "39000000"},
        {"binary": "EEEEEEEE"}
      ]
    }
    )EOF";

    Json::StringLoader config(json);
    TcpHealthCheckMatcher::MatchSegments segments =
        TcpHealthCheckMatcher::loadJsonBytes(config.getObjectArray("bytes"));
    EXPECT_EQ(2U, segments.size());
  }

  {
    std::string json = R"EOF(
    {
      "bytes": [
        {"binary": "4"}
      ]
    }
    )EOF";

    Json::StringLoader config(json);
    EXPECT_THROW(TcpHealthCheckMatcher::loadJsonBytes(config.getObjectArray("bytes")),
                 EnvoyException);
  }

  {
    std::string json = R"EOF(
    {
      "bytes": [
        {"binary": "gg"}
      ]
    }
    )EOF";

    Json::StringLoader config(json);
    EXPECT_THROW(TcpHealthCheckMatcher::loadJsonBytes(config.getObjectArray("bytes")),
                 EnvoyException);
  }
}

static void add_uint8(Buffer::Instance& buffer, uint8_t addend) {
  buffer.add(&addend, sizeof(addend));
}

TEST(TcpHealthCheckMatcher, match) {
  std::string json = R"EOF(
  {
    "bytes": [
      {"binary": "01"},
      {"binary": "02"}
    ]
  }
  )EOF";

  Json::StringLoader config(json);
  TcpHealthCheckMatcher::MatchSegments segments =
      TcpHealthCheckMatcher::loadJsonBytes(config.getObjectArray("bytes"));

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
  TcpHealthCheckerImplTest() : cluster_(new NiceMock<MockCluster>()) {
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

    Json::StringLoader config(json);
    health_checker_.reset(
        new TcpHealthCheckerImpl(*cluster_, config, dispatcher_, stats_, runtime_, random_));
  }

  void expectSessionCreate() {
    timeout_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
    interval_timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  }

  void expectClientCreate() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    EXPECT_CALL(*connection_, addReadFilter(_)).WillOnce(SaveArg<0>(&read_filter_));
    EXPECT_CALL(dispatcher_, createClientConnection_(_)).WillOnce(Return(connection_));
  }

  std::shared_ptr<MockCluster> cluster_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_;
  std::unique_ptr<TcpHealthCheckerImpl> health_checker_;
  Network::MockClientConnection* connection_{};
  Event::MockTimer* timeout_timer_{};
  Event::MockTimer* interval_timer_{};
  Network::ReadFilterPtr read_filter_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(TcpHealthCheckerImplTest, Success) {
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  expectSessionCreate();
  expectClientCreate();
  health_checker_->start();

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  Buffer::OwnedImpl response;
  add_uint8(response, 2);
  read_filter_->onData(response);
}

TEST_F(TcpHealthCheckerImplTest, Timeout) {
  health_checker_->start();

  expectSessionCreate();
  expectClientCreate();
  cluster_->hosts_ = {HostPtr{new HostImpl(*cluster_, "tcp://127.0.0.1:80", false, 1, "")}};
  cluster_->runCallbacks({cluster_->hosts_.back()}, {});

  Buffer::OwnedImpl response;
  add_uint8(response, 1);
  read_filter_->onData(response);

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_CALL(*connection_, close(_));
  timeout_timer_->callback_();
  EXPECT_TRUE(cluster_->hosts_[0]->healthy());

  expectClientCreate();
  interval_timer_->callback_();

  EXPECT_CALL(*timeout_timer_, disableTimer());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
  EXPECT_FALSE(cluster_->hosts_[0]->healthy());

  expectClientCreate();
  interval_timer_->callback_();

  std::vector<HostPtr> removed{cluster_->hosts_.back()};
  cluster_->hosts_.clear();
  EXPECT_CALL(*connection_, close(_));
  cluster_->runCallbacks({}, removed);
}

} // Upstream
