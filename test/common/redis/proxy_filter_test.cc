#include "common/redis/proxy_filter.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/redis/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::ByRef;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::WithArg;

namespace Redis {

TEST(RedisProxyFilterConfigTest, Normal) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<Upstream::MockClusterManager> cm;
  ProxyFilterConfig config(*json_config, cm);
  EXPECT_EQ("fake_cluster", config.clusterName());
}

TEST(RedisProxyFilterConfigTest, InvalidCluster) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("fake_cluster")).WillOnce(Return(nullptr));
  EXPECT_THROW(ProxyFilterConfig(*json_config, cm), EnvoyException);
}

TEST(RedisProxyFilterConfigTest, BadRedisProxyConfig) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "cluster": "fake_cluster"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ProxyFilterConfig(*json_config, cm), Json::Exception);
}

class RedisProxyFilterTest : public testing::Test, public DecoderFactory {
public:
  RedisProxyFilterTest() {
    filter_.initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter_.onNewConnection());
  }

  // Redis::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return DecoderPtr{decoder_};
  }

  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  DecoderCallbacks* decoder_callbacks_{};
  ConnPool::MockInstance conn_pool_;
  ProxyFilter filter_{*this, EncoderPtr{encoder_}, conn_pool_};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

TEST_F(RedisProxyFilterTest, Basic) {
  Buffer::OwnedImpl fake_data;
  ConnPool::MockActiveRequest request_handle1;
  ConnPool::ActiveRequestCallbacks* request_callbacks1;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        RespValuePtr request1(new RespValue());
        EXPECT_CALL(conn_pool_, makeRequest("", Ref(*request1), _))
            .WillOnce(
                DoAll(WithArg<2>(SaveArgAddress(&request_callbacks1)), Return(&request_handle1)));
        decoder_callbacks_->onRespValue(std::move(request1));
      }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_.onData(fake_data));

  RespValuePtr response1(new RespValue());
  EXPECT_CALL(*encoder_, encode(Ref(*response1), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_));
  request_callbacks1->onResponse(std::move(response1));

  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, UpstreamFailure) {
  Buffer::OwnedImpl fake_data;
  ConnPool::MockActiveRequest request_handle1;
  ConnPool::ActiveRequestCallbacks* request_callbacks1;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        RespValuePtr request1(new RespValue());
        EXPECT_CALL(conn_pool_, makeRequest("", Ref(*request1), _))
            .WillOnce(
                DoAll(WithArg<2>(SaveArgAddress(&request_callbacks1)), Return(&request_handle1)));
        decoder_callbacks_->onRespValue(std::move(request1));
      }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_.onData(fake_data));

  RespValue error;
  error.type(RespType::Error);
  error.asString() = "upstream connection error";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(error)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_));
  request_callbacks1->onFailure();

  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::LocalClose);
}

TEST_F(RedisProxyFilterTest, DownstreamDisconnectWithActive) {
  Buffer::OwnedImpl fake_data;
  ConnPool::MockActiveRequest request_handle1;
  ConnPool::ActiveRequestCallbacks* request_callbacks1;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        RespValuePtr request1(new RespValue());
        EXPECT_CALL(conn_pool_, makeRequest("", Ref(*request1), _))
            .WillOnce(
                DoAll(WithArg<2>(SaveArgAddress(&request_callbacks1)), Return(&request_handle1)));
        decoder_callbacks_->onRespValue(std::move(request1));
      }));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_.onData(fake_data));

  EXPECT_CALL(request_handle1, cancel());
  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, NoClient) {
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        RespValuePtr request1(new RespValue());
        EXPECT_CALL(conn_pool_, makeRequest("", Ref(*request1), _)).WillOnce(Return(nullptr));
        decoder_callbacks_->onRespValue(std::move(request1));
      }));

  RespValue error;
  error.type(RespType::Error);
  error.asString() = "no healthy upstream";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(error)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_.onData(fake_data));

  filter_callbacks_.connection_.raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisProxyFilterTest, ProtocolError) {
  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void { throw ProtocolError("error"); }));

  RespValue error;
  error.type(RespType::Error);
  error.asString() = "downstream protocol error";
  EXPECT_CALL(*encoder_, encode(Eq(ByRef(error)), _));
  EXPECT_CALL(filter_callbacks_.connection_, write(_));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_.onData(fake_data));
}

} // Redis
