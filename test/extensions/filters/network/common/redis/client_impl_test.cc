#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"

#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/common/redis/client_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Client {

envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings
createConnPoolSettings() {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy::ConnPoolSettings setting{};
  setting.mutable_op_timeout()->CopyFrom(Protobuf::util::TimeUtil::MillisecondsToDuration(20));
  setting.set_enable_hashtagging(true);
  return setting;
}

class RedisClientImplTest : public testing::Test, public Common::Redis::DecoderFactory {
public:
  // Commmon::Redis::DecoderFactory
  Common::Redis::DecoderPtr create(Common::Redis::DecoderCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    return Common::Redis::DecoderPtr{decoder_};
  }

  ~RedisClientImplTest() {
    client_.reset();

    // Make sure all gauges are 0.
    for (const Stats::GaugeSharedPtr& gauge : host_->cluster_.stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
    for (const Stats::GaugeSharedPtr& gauge : host_->stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
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
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_));
    EXPECT_CALL(*host_, createConnection_(_, _)).WillOnce(Return(conn_info));
    EXPECT_CALL(*upstream_connection_, addReadFilter(_))
        .WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*upstream_connection_, connect());
    EXPECT_CALL(*upstream_connection_, noDelay(true));

    client_ = ClientImpl::create(host_, dispatcher_, Common::Redis::EncoderPtr{encoder_}, *this,
                                 *config_);
    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_total_.value());
    EXPECT_EQ(1UL, host_->stats_.cx_total_.value());

    // NOP currently.
    upstream_connection_->runHighWatermarkCallbacks();
    upstream_connection_->runLowWatermarkCallbacks();
  }

  void onConnected() {
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_));
    upstream_connection_->raiseEvent(Network::ConnectionEvent::Connected);
  }

  const std::string cluster_name_{"foo"};
  std::shared_ptr<Upstream::MockHost> host_{new NiceMock<Upstream::MockHost>()};
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* connect_or_op_timer_{new Event::MockTimer(&dispatcher_)};
  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  Common::Redis::DecoderCallbacks* callbacks_{};
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  std::unique_ptr<Config> config_;
  ClientPtr client_;
};


TEST_F(RedisClientImplTest, Basic) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
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
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_));
    EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::SUCCESS));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::SUCCESS));
    callbacks_->onRespValue(std::move(response2));
  }));
  upstream_read_filter_->onData(fake_data, false);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  client_->close();
}

TEST_F(RedisClientImplTest, Cancel) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Common::Redis::RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  handle1->cancel();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    InSequence s;

    Common::Redis::RespValuePtr response1(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks1, onResponse_(_)).Times(0);
    EXPECT_CALL(*connect_or_op_timer_, enableTimer(_));
    EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::SUCCESS));
    callbacks_->onRespValue(std::move(response1));

    Common::Redis::RespValuePtr response2(new Common::Redis::RespValue());
    EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
    EXPECT_CALL(*connect_or_op_timer_, disableTimer());
    EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::SUCCESS));
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
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::SERVER_FAILURE));
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
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw Common::Redis::ProtocolError("error");
  }));
  EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::REQUEST_FAILED));
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
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::SERVER_FAILURE));
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
};

TEST_F(RedisClientImplTest, OutlierDisabled) {
  InSequence s;

  setup(std::make_unique<ConfigOutlierDisabled>());

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_, putResult(_)).Times(0);
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
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::TIMEOUT));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  connect_or_op_timer_->callback_();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_connect_timeout_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

TEST_F(RedisClientImplTest, OpTimeout) {
  InSequence s;

  setup();

  Common::Redis::RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

  EXPECT_CALL(host_->outlier_detector_, putResult(Upstream::Outlier::Result::TIMEOUT));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_or_op_timer_, disableTimer());
  connect_or_op_timer_->callback_();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_timeout_.value());
  EXPECT_EQ(1UL, host_->stats_.rq_timeout_.value());
}

TEST(RedisClientFactoryImplTest, Basic) {
  ClientFactoryImpl factory;
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = new NiceMock<Network::MockClientConnection>();
  std::shared_ptr<Upstream::MockHost> host(new NiceMock<Upstream::MockHost>());
  EXPECT_CALL(*host, createConnection_(_, _)).WillOnce(Return(conn_info));
  NiceMock<Event::MockDispatcher> dispatcher;
  ConfigImpl config(createConnPoolSettings());
  ClientPtr client = factory.create(host, dispatcher, config);
  client->close();
}

} // namespace Client 
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
