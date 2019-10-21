#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"
#include "extensions/filters/network/common/redis/utility.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/common/redis/test_utils.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

class RedisClientImplTest : public testing::Test, public Common::Redis::DecoderFactory {
public:
  // Common::Redis::DecoderFactory
  Common::Redis::DecoderPtr create(Common::Redis::DecoderCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    return Common::Redis::DecoderPtr{decoder_};
  }

  ~RedisClientImplTest() override {
    client_.reset();

    EXPECT_TRUE(TestUtility::gaugesZeroed(host_->cluster_.stats_store_.gauges()));
    EXPECT_TRUE(TestUtility::gaugesZeroed(host_->stats_.gauges()));
  }

  void setup() {
    config_ = std::make_unique<ConfigImpl>(createConnPoolSettings());
    finishSetup();
  }

  void setup(std::unique_ptr<Config>&& config) {
    config_ = std::move(config);
    finishSetup();
  }

  void finishSetup() {
    upstream_connection_ = new NiceMock<Network::MockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    conn_info.connection_ = upstream_connection_;

    // Create timers in order they are created in client_impl.cc
    connect_or_op_timer_ = new Event::MockTimer(&dispatcher_);
    flush_timer_ = new Event::MockTimer(&dispatcher_);

    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(*host_, createConnection_(_, _)).WillOnce(Return(conn_info));
    EXPECT_CALL(*upstream_connection_, addReadFilter(_))
        .WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*upstream_connection_, connect());
    EXPECT_CALL(*upstream_connection_, noDelay(true));

    redis_command_stats_ =
        Common::Redis::RedisCommandStats::createRedisCommandStats(stats_.symbolTable());

    client_ = ClientImpl::create(host_, dispatcher_, Common::Redis::EncoderPtr{encoder_}, *this,
                                 *config_, redis_command_stats_, stats_);
    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_total_.value());
    EXPECT_EQ(1UL, host_->stats_.cx_total_.value());
    EXPECT_EQ(false, client_->active());

    // NOP currently.
    upstream_connection_->runHighWatermarkCallbacks();
    upstream_connection_->runLowWatermarkCallbacks();
  }

  void onConnected() {
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);
  }

  void respond() {
    Common::Redis::RespValuePtr response1{new Common::Redis::RespValue()};
    response1->type(Common::Redis::RespType::SimpleString);
    response1->asString() = "OK";
    EXPECT_EQ(true, client_->active());
    ClientImpl* client_impl = dynamic_cast<ClientImpl*>(client_.get());
    EXPECT_NE(client_impl, nullptr);
    client_impl->onRespValue(std::move(response1));
  }

  void testInitializeReadPolicy(
      envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings::ReadPolicy
          read_policy) {
    InSequence s;

    setup(std::make_unique<ConfigImpl>(createConnPoolSettings(20, true, true, 100, read_policy)));

    Common::Redis::RespValue readonly_request = Utility::ReadOnlyRequest::instance();
    EXPECT_CALL(*encoder_, encode(Eq(readonly_request), _));
    EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
    client_->initialize(auth_password_);

    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_total_.value());
    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_active_.value());
    EXPECT_EQ(1UL, host_->stats_.rq_total_.value());
    EXPECT_EQ(1UL, host_->stats_.rq_active_.value());

    EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    client_->close();
  }

  const std::string cluster_name_{"foo"};
  std::shared_ptr<Upstream::MockHost> host_{new NiceMock<Upstream::MockHost>()};
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* flush_timer_{};
  Event::MockTimer* connect_or_op_timer_{};
  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  Common::Redis::DecoderCallbacks* callbacks_{};
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  std::unique_ptr<Config> config_;
  ClientPtr client_;
  Stats::IsolatedStoreImpl stats_;
  Stats::ScopePtr stats_scope_;
  Common::Redis::RedisCommandStatsSharedPtr redis_command_stats_;
  std::string auth_password_;
};

TEST_F(RedisClientImplTest, BatchWithZeroBufferAndTimeout) {
  // Basic test with a single request, default buffer size (0) and timeout (0).
  // This means we do not batch requests, and thus the flush timer is never enabled.
  InSequence s;

  setup();

  // Make the dummy request
  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  // Process the dummy request
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

class ConfigBufferSizeGTSingleRequest : public Config {
  bool disableOutlierEvents() const override { return false; }
  std::chrono::milliseconds opTimeout() const override { return std::chrono::milliseconds(25); }
  bool enableHashtagging() const override { return false; }
  bool enableRedirection() const override { return false; }
  unsigned int maxBufferSizeBeforeFlush() const override { return 8; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return std::chrono::milliseconds(1);
  }
  uint32_t maxUpstreamUnknownConnections() const override { return 0; }
  bool enableCommandStats() const override { return false; }
  ReadPolicy readPolicy() const override { return ReadPolicy::Master; }
};

TEST_F(RedisClientImplTest, BatchWithTimerFiring) {
  // With a flush buffer > single request length, the flush timer comes into play.
  // In this test, we make a single request that doesn't fill the buffer, so we
  // have to wait for the flush timer to fire.
  InSequence s;

  setup(std::make_unique<ConfigBufferSizeGTSingleRequest>());

  // Make the dummy request
  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enableTimer(_, _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  // Pretend the flush timer fires.
  // The timer callback is the general-purpose flush function, also used when
  // the buffer is filled. If the buffer fills before the timer fires, we need
  // to check if the timer is active and cancel it. However, if the timer fires
  // the callback, this internal check returns false as the timer is finished.
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  flush_timer_->invokeCallback();

  // Process the dummy request
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, BatchWithTimerCancelledByBufferFlush) {
  // Expanding on the previous test, let's the flush buffer is filled by two requests.
  // In this test, we make a single request that doesn't fill the buffer, and the timer
  // starts. However, a second request comes in, which should cancel the timer, such
  // that it is never invoked.
  InSequence s;

  setup(std::make_unique<ConfigBufferSizeGTSingleRequest>());

  // Make the dummy request (doesn't fill buffer, starts timer)
  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enableTimer(_, _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  // Make a second dummy request (fills buffer, cancels timer)
  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(true));
  ;
  EXPECT_CALL(*flush_timer_, disableTimer());
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  // Process the dummy requests
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, Basic) {
  InSequence s;

  setup();

  client_->initialize(auth_password_);

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, InitializedWithAuthPassword) {
  InSequence s;

  setup();

  auth_password_ = "testing password";
  Common::Redis::RespValue auth_request = Utility::makeAuthCommand(auth_password_);
  EXPECT_CALL(*encoder_, encode(Eq(auth_request), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  client_->initialize(auth_password_);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_active_.value());

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, InitializedWithPreferMasterReadPolicy) {
  testInitializeReadPolicy(envoy::config::filter::network::redis_proxy::v2::
                               RedisProxy_ConnPoolSettings_ReadPolicy_PREFER_MASTER);
}

TEST_F(RedisClientImplTest, InitializedWithReplicaReadPolicy) {
  testInitializeReadPolicy(envoy::config::filter::network::redis_proxy::v2::
                               RedisProxy_ConnPoolSettings_ReadPolicy_REPLICA);
}

TEST_F(RedisClientImplTest, InitializedWithPreferReplicaReadPolicy) {
  testInitializeReadPolicy(envoy::config::filter::network::redis_proxy::v2::
                               RedisProxy_ConnPoolSettings_ReadPolicy_PREFER_REPLICA);
}

TEST_F(RedisClientImplTest, InitializedWithAnyReadPolicy) {
  testInitializeReadPolicy(
      envoy::config::filter::network::redis_proxy::v2::RedisProxy_ConnPoolSettings_ReadPolicy_ANY);
}

TEST_F(RedisClientImplTest, Cancel) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  handle1->cancel();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;

    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(_)).Times(0);
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_cancelled_.value());
}

TEST_F(RedisClientImplTest, FailAll) {
  InSequence s;

  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LOCAL_ORIGIN_CONNECT_FAILED, _));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_remote_with_active_rq_.value());
}

TEST_F(RedisClientImplTest, FailAllWithCancel) {
  InSequence s;

  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();
  handle1->cancel();

  EXPECT_CALL(callbacks1, onFailure()).Times(0);
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_local_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_cancelled_.value());
}

TEST_F(RedisClientImplTest, ProtocolError) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw Common::Redis::ProtocolError("error");
  }));
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_FAILED, _));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_protocol_error_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_error_.value());
}

TEST_F(RedisClientImplTest, ConnectFail) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LOCAL_ORIGIN_CONNECT_FAILED, _));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

class ConfigOutlierDisabled : public Config {
  bool disableOutlierEvents() const override { return true; }
  std::chrono::milliseconds opTimeout() const override { return std::chrono::milliseconds(25); }
  bool enableHashtagging() const override { return false; }
  bool enableRedirection() const override { return false; }
  unsigned int maxBufferSizeBeforeFlush() const override { return 0; }
  std::chrono::milliseconds bufferFlushTimeoutInMs() const override {
    return std::chrono::milliseconds(0);
  }
  ReadPolicy readPolicy() const override { return ReadPolicy::Master; }
  uint32_t maxUpstreamUnknownConnections() const override { return 0; }
  bool enableCommandStats() const override { return false; }
};

TEST_F(RedisClientImplTest, OutlierDisabled) {
  InSequence s;

  setup(std::make_unique<ConfigOutlierDisabled>());

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_, putResult(_, _)).Times(0);
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

TEST_F(RedisClientImplTest, ConnectTimeout) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LOCAL_ORIGIN_TIMEOUT, _));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  connect_or_op_timer_->invokeCallback();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_connect_timeout_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

TEST_F(RedisClientImplTest, OpTimeout) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_active_.value());

  EXPECT_CALL(callbacks1, onResponse_(_));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
  respond();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_rq_active_.value());

  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
  handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LOCAL_ORIGIN_TIMEOUT, _));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  connect_or_op_timer_->invokeCallback();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_timeout_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_timeout_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_rq_active_.value());
}

TEST_F(RedisClientImplTest, AskRedirection) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "ASK 1111 10.1.2.3:4321";
    // Simulate redirection failure.
    EXPECT_CALL(callbacks1, onRedirection(Ref(*response1))).WillOnce(Return(false));
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_internal_redirect_failed_total_.value());

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "ASK 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onRedirection(Ref(*response2))).WillOnce(Return(true));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_internal_redirect_succeeded_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, MovedRedirection) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "MOVED 1111 10.1.2.3:4321";
    // Simulate redirection failure.
    EXPECT_CALL(callbacks1, onRedirection(Ref(*response1))).WillOnce(Return(false));
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_internal_redirect_failed_total_.value());

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "MOVED 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onRedirection(Ref(*response2))).WillOnce(Return(true));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_internal_redirect_succeeded_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, AskRedirectionNotEnabled) {
  InSequence s;

  setup(std::make_unique<ConfigImpl>(createConnPoolSettings(20, true, false)));

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "ASK 1111 10.1.2.3:4321";
    // Simulate redirection failure.
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_failed_total_.value());
    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_succeeded_total_.value());

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "ASK 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_failed_total_.value());
    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_succeeded_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, MovedRedirectionNotEnabled) {
  InSequence s;

  setup(std::make_unique<ConfigImpl>(createConnPoolSettings(20, true, false)));

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;
    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    response1->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response1->asString() = "MOVED 1111 10.1.2.3:4321";
    EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_, _));
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response1));

    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_succeeded_total_.value());
    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_failed_total_.value());

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    response2->type(Common::Redis::RespType::Error);
    // The exact values of the hash slot and IP info are not important.
    response2->asString() = "MOVED 2222 10.1.2.4:4321";
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_,
                putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
    callbacks_->onRespValue(std::move(response2));

    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_succeeded_total_.value());
    EXPECT_EQ(0UL, host_->cluster_.stats_.upstream_internal_redirect_failed_total_.value());
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, RemoveFailedHealthCheck) {
  // This test simulates a health check response signaling traffic should be drained from the host.
  // As a result, the health checker will close the client in the call back.
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
  // Each call should result in either onResponse or onFailure, never both.
  EXPECT_CALL(callbacks1, onFailure()).Times(0);
  EXPECT_CALL(callbacks1, onResponse_(Ref(response1)))
      .WillOnce(Invoke([&](Common::Redis::RespValuePtr&) {
        // The health checker might fail the active health check based on the response content, and
        // result in removing the host and closing the client.
        client_->close();
      }));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer()).Times(2);
  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::EXT_ORIGIN_REQUEST_SUCCESS, _));
  callbacks_->onRespValue(std::move(response1));
}

TEST_F(RedisClientImplTest, RemoveFailedHost) {
  // This test simulates a health check request failed due to remote host closing the connection.
  // As a result the health checker will close the client in the call back.
  InSequence s;

  setup();

  NiceMock<Network::MockConnectionCallbacks> connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  EXPECT_CALL(*flush_timer_, enabled()).WillOnce(Return(false));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_CALL(host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LOCAL_ORIGIN_CONNECT_FAILED, _));
  EXPECT_CALL(callbacks1, onFailure()).WillOnce(Invoke([&]() { client_->close(); }));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  upstream_connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST(RedisClientFactoryImplTest, Basic) {
  ClientFactoryImpl factory;
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = new NiceMock<Network::MockClientConnection>();
  std::shared_ptr<Upstream::MockHost> host(new NiceMock<Upstream::MockHost>());
  EXPECT_CALL(*host, createConnection_(_, _)).WillOnce(Return(conn_info));
  NiceMock<Event::MockDispatcher> dispatcher;
  ConfigImpl config(createConnPoolSettings());
  Stats::IsolatedStoreImpl stats_;
  auto redis_command_stats =
      Common::Redis::RedisCommandStats::createRedisCommandStats(stats_.symbolTable());
  const std::string auth_password;
  ClientPtr client =
      factory.create(host, dispatcher, config, redis_command_stats, stats_, auth_password);
  client->close();
}
} // namespace Client
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
