#include <memory>
#include <string>

#include "common/config/filter_json.h"
#include "common/redis/proxy_filter.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/redis/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ByRef;
using testing::DoAll;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Redis {

envoy::config::filter::network::redis_proxy::v2::RedisProxy
parseProtoFromJson(const std::string& json_string) {
  envoy::config::filter::network::redis_proxy::v2::RedisProxy config;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateRedisProxy(*json_object_ptr, config);

  return config;
}

class RedisProxyFilterConfigTest : public testing::Test {
public:
  NiceMock<Upstream::MockClusterManager> cm_;
  Stats::IsolatedStoreImpl store_;
  Network::MockDrainDecision drain_decision_;
  Runtime::MockLoader runtime_;
};

TEST_F(RedisProxyFilterConfigTest, Normal) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": { "op_timeout_ms" : 10 }
  }
  )EOF";

  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
      Envoy::Redis::parseProtoFromJson(json_string);
  ProxyFilterConfig config(proto_config, cm_, store_, drain_decision_, runtime_);
  EXPECT_EQ("fake_cluster", config.cluster_name_);
}

TEST_F(RedisProxyFilterConfigTest, InvalidCluster) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": { "op_timeout_ms" : 10 }
  }
  )EOF";

  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
      Envoy::Redis::parseProtoFromJson(json_string);

  EXPECT_CALL(cm_, get("fake_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW_WITH_MESSAGE(ProxyFilterConfig(proto_config, cm_, store_, drain_decision_, runtime_),
                            EnvoyException, "redis: unknown cluster 'fake_cluster'");
}

TEST_F(RedisProxyFilterConfigTest, InvalidAddedByApi) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": { "op_timeout_ms" : 10 }
  }
  )EOF";

  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
      Envoy::Redis::parseProtoFromJson(json_string);

  ON_CALL(*cm_.thread_local_cluster_.cluster_.info_, addedViaApi()).WillByDefault(Return(true));
  EXPECT_THROW_WITH_MESSAGE(ProxyFilterConfig(proto_config, cm_, store_, drain_decision_, runtime_),
                            EnvoyException,
                            "redis: invalid cluster 'fake_cluster': currently only "
                            "static (non-CDS) clusters are supported");
}

TEST_F(RedisProxyFilterConfigTest, BadRedisProxyConfig) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "cluster": "fake_cluster"
  }
  )EOF";

  EXPECT_THROW(parseProtoFromJson(json_string), Json::Exception);
}

class RedisProxyFilterTest : public testing::Test, public DecoderFactory {
public:
  RedisProxyFilterTest() {
    std::string json_string = R"EOF(
    {
      "cluster_name": "fake_cluster",
      "stat_prefix": "foo",
      "conn_pool": { "op_timeout_ms" : 10 }
    }
    )EOF";

    envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
        Envoy::Redis::parseProtoFromJson(json_string);
    NiceMock<Upstream::MockClusterManager> cm;
    config_.reset(new ProxyFilterConfig(proto_config, cm, store_, drain_decision_, runtime_));
    filter_.reset(new ProxyFilter(*this, EncoderPtr{encoder_}, splitter_, config_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_total_.value());
    EXPECT_EQ(1UL, config_->stats_.downstream_cx_active_.value());

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  ~RedisProxyFilterTest() {
    filter_.reset();
    for (const Stats::GaugeSharedPtr& gauge : store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  // Redis::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return DecoderPtr{decoder_};
  }

  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  DecoderCallbacks* decoder_callbacks_{};
  CommandSplitter::MockInstance splitter_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  NiceMock<Runtime::MockLoader> runtime_;
  ProxyFilterConfigSharedPtr config_;
  std::unique_ptr<ProxyFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

TEST_F(RedisProxyFilterTest, OutOfOrderResponseWithDrainClose) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* request_handle1 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks1;
  CommandSplitter::MockSplitRequest* request_handle2 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks2;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    RespValuePtr request1(new RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1)), Return(request_handle1)));
    decoder_callbacks_->onRespValue(std::move(request1));

    RespValuePtr request2(new RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request2), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks2)), Return(request_handle2)));
    decoder_callbacks_->onRespValue(std::move(request2));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data));

  EXPECT_EQ(2UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(2UL, config_->stats_.downstream_rq_active_.value());

  RespValuePtr response2(new RespValue());
  RespValue* response2_ptr = response2.get();
  request_callbacks2->onResponse(std::move(response2));

  RespValuePtr response1(new RespValue());
  EXPECT_CALL(*encoder_, encode(Ref(*response1), _));
  EXPECT_CALL(*encoder_, encode(Ref(*response2_ptr), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_));
  EXPECT_CALL(drain_decision_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("redis.drain_close_enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  request_callbacks1->onResponse(std::move(response1));

  EXPECT_EQ(1UL, config_->stats_.downstream_cx_drain_close_.value());
}

TEST_F(RedisProxyFilterTest, OutOfOrderResponseDownstreamDisconnectBeforeFlush) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* request_handle1 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks1;
  CommandSplitter::MockSplitRequest* request_handle2 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks2;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    RespValuePtr request1(new RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1)), Return(request_handle1)));
    decoder_callbacks_->onRespValue(std::move(request1));

    RespValuePtr request2(new RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request2), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks2)), Return(request_handle2)));
    decoder_callbacks_->onRespValue(std::move(request2));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data));

  EXPECT_EQ(2UL, config_->stats_.downstream_rq_total_.value());
  EXPECT_EQ(2UL, config_->stats_.downstream_rq_active_.value());

  RespValuePtr response2(new RespValue());
  request_callbacks2->onResponse(std::move(response2));
  EXPECT_CALL(*request_handle1, cancel());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, DownstreamDisconnectWithActive) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  CommandSplitter::MockSplitRequest* request_handle1 = new CommandSplitter::MockSplitRequest();
  CommandSplitter::SplitCallbacks* request_callbacks1;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    RespValuePtr request1(new RespValue());
    EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&request_callbacks1)), Return(request_handle1)));
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data));

  EXPECT_CALL(*request_handle1, cancel());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ImmediateResponse) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  RespValuePtr request1(new RespValue());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    decoder_callbacks_->onRespValue(std::move(request1));
  }));
  EXPECT_CALL(splitter_, makeRequest_(Ref(*request1), _))
      .WillOnce(
          Invoke([&](const RespValue&,
                     CommandSplitter::SplitCallbacks& callbacks) -> CommandSplitter::SplitRequest* {
            RespValuePtr error(new RespValue());
            error->type(RespType::Error);
            error->asString() = "no healthy upstream";
            EXPECT_CALL(*encoder_, encode(Eq(ByRef(*error)), _));
            EXPECT_CALL(filter_callbacks_.connection_, write(_));
            callbacks.onResponse(std::move(error));
            return nullptr;
          }));

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(fake_data));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ProtocolError) {
  InSequence s;

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data))).WillOnce(Invoke([&](Buffer::Instance&) -> void {
    throw ProtocolError("error");
  }));

  RespValue error;
  error.type(RespType::Error);
  error.asString() = "downstream protocol error";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(error)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_data));

  EXPECT_EQ(1UL, store_.counter("redis.foo.downstream_cx_protocol_error").value());
}

} // namespace Redis
} // namespace Envoy
