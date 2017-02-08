#include "common/network/utility.h"
#include "common/redis/conn_pool_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/redis/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Ref;
using testing::Return;
using testing::SaveArg;

namespace Redis {
namespace ConnPool {

class RedisClientImplTest : public testing::Test, public DecoderFactory {
public:
  // Redis::DecoderFactory
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    return DecoderPtr{decoder_};
  }

  void setup() {
    upstream_connection_ = new NiceMock<Network::MockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    conn_info.connection_ = upstream_connection_;
    conn_info.host_.reset(new Upstream::HostImpl(
        cm_.cluster_.info_, Network::Utility::resolveUrl("tcp://127.0.0.1:80"), false, 1, ""));
    EXPECT_CALL(cm_, tcpConnForCluster_("foo")).WillOnce(Return(conn_info));
    EXPECT_CALL(*upstream_connection_, addReadFilter(_))
        .WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*upstream_connection_, connect());
    EXPECT_CALL(*upstream_connection_, noDelay(true));
    client_ = ClientImpl::create(cluster_name_, cm_, EncoderPtr{encoder_}, *this);
  }

  const std::string cluster_name_{"foo"};
  Upstream::MockClusterManager cm_;
  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  DecoderCallbacks* callbacks_{};
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterPtr upstream_read_filter_;
  ClientPtr client_;
};

TEST_F(RedisClientImplTest, NoClient) {
  Upstream::MockHost::MockCreateConnectionData conn_info;
  EXPECT_CALL(cm_, tcpConnForCluster_("foo")).WillOnce(Return(conn_info));
  EXPECT_EQ(nullptr, ClientImpl::create(cluster_name_, cm_, EncoderPtr{encoder_}, *this));

  // In this test decoder is not consumed so we need to delete it.
  delete decoder_;
}

TEST_F(RedisClientImplTest, Basic) {
  setup();

  RespValue request1;
  MockActiveRequestCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  ActiveRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  RespValue request2;
  MockActiveRequestCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  ActiveRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        InSequence s;
        RespValuePtr response1(new RespValue());
        EXPECT_CALL(callbacks1, onResponse_(Ref(response1)));
        callbacks_->onRespValue(std::move(response1));

        RespValuePtr response2(new RespValue());
        EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
        callbacks_->onRespValue(std::move(response2));
      }));
  upstream_read_filter_->onData(fake_data);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  client_->close();
}

TEST_F(RedisClientImplTest, Cancel) {
  setup();

  RespValue request1;
  MockActiveRequestCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  ActiveRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  RespValue request2;
  MockActiveRequestCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  ActiveRequest* handle2 = client_->makeRequest(request2, callbacks2);
  EXPECT_NE(nullptr, handle2);

  handle1->cancel();

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void {
        InSequence s;
        RespValuePtr response1(new RespValue());
        EXPECT_CALL(callbacks1, onResponse_(_)).Times(0);
        callbacks_->onRespValue(std::move(response1));

        RespValuePtr response2(new RespValue());
        EXPECT_CALL(callbacks2, onResponse_(Ref(response2)));
        callbacks_->onRespValue(std::move(response2));
      }));
  upstream_read_filter_->onData(fake_data);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  client_->close();
}

TEST_F(RedisClientImplTest, FailAll) {
  setup();

  Network::MockConnectionCallbacks connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  RespValue request1;
  MockActiveRequestCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  ActiveRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(callbacks1, onFailure());
  upstream_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisClientImplTest, ProtocolError) {
  setup();

  RespValue request1;
  MockActiveRequestCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  ActiveRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void { throw ProtocolError("error"); }));
  upstream_read_filter_->onData(fake_data);
}

TEST(RedisClientFactoryImplTest, Basic) {
  ClientFactoryImpl factory;
  Upstream::MockClusterManager cm;
  EXPECT_CALL(cm, tcpConnForCluster_("foo"));
  EXPECT_EQ(nullptr, factory.create("foo", cm));
}

class RedisConnPoolImplTest : public testing::Test, public ClientFactory {
public:
  // Redis::ConnPool::ClientFactory
  ClientPtr create(const std::string& cluster_name, Upstream::ClusterManager& cm) override {
    return ClientPtr{create_(cluster_name, cm)};
  }

  MOCK_METHOD2(create_, Client*(const std::string& cluster_name, Upstream::ClusterManager& cm));

  const std::string cluster_name_{"foo"};
  Upstream::MockClusterManager cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  InstanceImpl conn_pool_{cluster_name_, cm_, *this, tls_};
};

TEST_F(RedisConnPoolImplTest, Basic) {
  RespValue value;
  MockActiveRequest active_request;
  MockActiveRequestCallbacks callbacks;
  MockClient* client = new NiceMock<MockClient>();

  EXPECT_CALL(*this, create_("foo", _)).WillOnce(Return(client));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  ActiveRequest* request = conn_pool_.makeRequest("foo", value, callbacks);
  EXPECT_EQ(&active_request, request);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, NoClient) {
  RespValue value;
  MockActiveRequestCallbacks callbacks;
  EXPECT_CALL(*this, create_("foo", _)).WillOnce(Return(nullptr));
  ActiveRequest* request = conn_pool_.makeRequest("foo", value, callbacks);
  EXPECT_EQ(nullptr, request);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, RemoteClose) {
  RespValue value;
  MockActiveRequest active_request;
  MockActiveRequestCallbacks callbacks;
  MockClient* client = new NiceMock<MockClient>();

  EXPECT_CALL(*this, create_("foo", _)).WillOnce(Return(client));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  conn_pool_.makeRequest("foo", value, callbacks);

  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  client->raiseEvents(Network::ConnectionEvent::RemoteClose);

  tls_.shutdownThread();
}

} // ConnPool
} // Redis
