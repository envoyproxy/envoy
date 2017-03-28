#include "common/network/utility.h"
#include "common/redis/conn_pool_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/redis/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

using testing::_;
using testing::Eq;
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
    std::shared_ptr<Upstream::MockHost> host(new Upstream::MockHost());
    conn_info.connection_ = upstream_connection_;
    EXPECT_CALL(*host, createConnection_(_)).WillOnce(Return(conn_info));
    EXPECT_CALL(*upstream_connection_, addReadFilter(_))
        .WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*upstream_connection_, connect());
    EXPECT_CALL(*upstream_connection_, noDelay(true));
    client_ = ClientImpl::create(host, dispatcher_, EncoderPtr{encoder_}, *this);
  }

  const std::string cluster_name_{"foo"};
  Event::MockDispatcher dispatcher_;
  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  DecoderCallbacks* callbacks_{};
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  ClientPtr client_;
};

TEST_F(RedisClientImplTest, Basic) {
  setup();

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
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
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  RespValue request2;
  MockPoolCallbacks callbacks2;
  EXPECT_CALL(*encoder_, encode(Ref(request2), _));
  PoolRequest* handle2 = client_->makeRequest(request2, callbacks2);
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
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  EXPECT_CALL(callbacks1, onFailure());
  upstream_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RedisClientImplTest, ProtocolError) {
  setup();

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
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
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = new NiceMock<Network::MockClientConnection>();
  std::shared_ptr<Upstream::MockHost> host(new Upstream::MockHost());
  EXPECT_CALL(*host, createConnection_(_)).WillOnce(Return(conn_info));
  Event::MockDispatcher dispatcher;
  ClientPtr client = factory.create(host, dispatcher);
  client->close();
}

class RedisConnPoolImplTest : public testing::Test, public ClientFactory {
public:
  // Redis::ConnPool::ClientFactory
  ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher) override {
    return ClientPtr{create_(host, dispatcher)};
  }

  MOCK_METHOD2(create_, Client*(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher));

  const std::string cluster_name_{"foo"};
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  InstanceImpl conn_pool_{cluster_name_, cm_, *this, tls_};
};

TEST_F(RedisConnPoolImplTest, Basic) {
  InSequence s;

  RespValue value;
  MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  MockClient* client = new NiceMock<MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_))
      .WillOnce(
          Invoke([&](const Upstream::LoadBalancerContext* context) -> Upstream::HostConstSharedPtr {
            EXPECT_EQ(context->hashKey().value(), std::hash<std::string>()("foo"));
            return cm_.thread_local_cluster_.lb_.host_;
          }));
  EXPECT_CALL(*this, create_(_, _)).WillOnce(Return(client));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  PoolRequest* request = conn_pool_.makeRequest("foo", value, callbacks);
  EXPECT_EQ(&active_request, request);

  EXPECT_CALL(*client, close());
  tls_.shutdownThread();
};

TEST_F(RedisConnPoolImplTest, HostRemove) {
  InSequence s;
  MockPoolCallbacks callbacks;

  RespValue value;
  std::shared_ptr<Upstream::Host> host1(new Upstream::MockHost());
  std::shared_ptr<Upstream::Host> host2(new Upstream::MockHost());
  MockClient* client1 = new NiceMock<MockClient>();
  MockClient* client2 = new NiceMock<MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host1));
  EXPECT_CALL(*this, create_(Eq(host1), _)).WillOnce(Return(client1));

  MockPoolRequest active_request1;
  EXPECT_CALL(*client1, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request1));
  PoolRequest* request1 = conn_pool_.makeRequest("foo", value, callbacks);
  EXPECT_EQ(&active_request1, request1);

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(host2));
  EXPECT_CALL(*this, create_(Eq(host2), _)).WillOnce(Return(client2));

  MockPoolRequest active_request2;
  EXPECT_CALL(*client2, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request2));
  PoolRequest* request2 = conn_pool_.makeRequest("bar", value, callbacks);
  EXPECT_EQ(&active_request2, request2);

  EXPECT_CALL(*client2, close());
  cm_.thread_local_cluster_.cluster_.runCallbacks({}, {host2});

  EXPECT_CALL(*client1, close());
  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, NoHost) {
  InSequence s;

  RespValue value;
  MockPoolCallbacks callbacks;
  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_)).WillOnce(Return(nullptr));
  PoolRequest* request = conn_pool_.makeRequest("foo", value, callbacks);
  EXPECT_EQ(nullptr, request);

  tls_.shutdownThread();
}

TEST_F(RedisConnPoolImplTest, RemoteClose) {
  InSequence s;

  RespValue value;
  MockPoolRequest active_request;
  MockPoolCallbacks callbacks;
  MockClient* client = new NiceMock<MockClient>();

  EXPECT_CALL(cm_.thread_local_cluster_.lb_, chooseHost(_));
  EXPECT_CALL(*this, create_(_, _)).WillOnce(Return(client));
  EXPECT_CALL(*client, makeRequest(Ref(value), Ref(callbacks))).WillOnce(Return(&active_request));
  conn_pool_.makeRequest("foo", value, callbacks);

  EXPECT_CALL(tls_.dispatcher_, deferredDelete_(_));
  client->raiseEvents(Network::ConnectionEvent::RemoteClose);

  tls_.shutdownThread();
}

} // ConnPool
} // Redis
