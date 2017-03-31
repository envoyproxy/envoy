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

  ~RedisClientImplTest() {
    client_.reset();

    // Make sure all gauges are 0.
    for (Stats::GaugeSharedPtr gauge : host_->cluster_.stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
    for (Stats::GaugeSharedPtr gauge : host_->stats_store_.gauges()) {
      EXPECT_EQ(0U, gauge->value());
    }
  }

  void setup() {
    upstream_connection_ = new NiceMock<Network::MockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    conn_info.connection_ = upstream_connection_;
    EXPECT_CALL(*connect_timer_, enableTimer(_));
    EXPECT_CALL(*host_, createConnection_(_)).WillOnce(Return(conn_info));
    EXPECT_CALL(*upstream_connection_, addReadFilter(_))
        .WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*upstream_connection_, connect());
    EXPECT_CALL(*upstream_connection_, noDelay(true));
    client_ = ClientImpl::create(host_, dispatcher_, EncoderPtr{encoder_}, *this);
    EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_total_.value());
    EXPECT_EQ(1UL, host_->stats_.cx_total_.value());
  }

  void onConnected() {
    EXPECT_CALL(*connect_timer_, disableTimer());
    upstream_connection_->raiseEvents(Network::ConnectionEvent::Connected);
  }

  const std::string cluster_name_{"foo"};
  std::shared_ptr<Upstream::MockHost> host_{new NiceMock<Upstream::MockHost>()};
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* connect_timer_{new Event::MockTimer(&dispatcher_)};
  MockEncoder* encoder_{new MockEncoder()};
  MockDecoder* decoder_{new MockDecoder()};
  DecoderCallbacks* callbacks_{};
  NiceMock<Network::MockClientConnection>* upstream_connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  ClientPtr client_;
};

TEST_F(RedisClientImplTest, Basic) {
  InSequence s;

  setup();
  onConnected();

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

  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_total_.value());
  EXPECT_EQ(2UL, host_->cluster_.stats_.upstream_rq_active_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_total_.value());
  EXPECT_EQ(2UL, host_->stats_.rq_active_.value());

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
  InSequence s;

  setup();

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  onConnected();

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

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_cancelled_.value());
}

TEST_F(RedisClientImplTest, FailAll) {
  InSequence s;

  setup();
  onConnected();

  Network::MockConnectionCallbacks connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  upstream_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_remote_with_active_rq_.value());
}

TEST_F(RedisClientImplTest, FailAllWithCancel) {
  InSequence s;

  setup();
  onConnected();

  Network::MockConnectionCallbacks connection_callbacks;
  client_->addConnectionCallbacks(connection_callbacks);

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);
  handle1->cancel();

  EXPECT_CALL(callbacks1, onFailure()).Times(0);
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  upstream_connection_->raiseEvents(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_destroy_local_with_active_rq_.value());
  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_rq_cancelled_.value());
}

TEST_F(RedisClientImplTest, ProtocolError) {
  InSequence s;

  setup();
  onConnected();

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  Buffer::OwnedImpl fake_data;
  EXPECT_CALL(*decoder_, decode(Ref(fake_data)))
      .WillOnce(Invoke([&](Buffer::Instance&) -> void { throw ProtocolError("error"); }));
  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  upstream_read_filter_->onData(fake_data);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_protocol_error_.value());
}

TEST_F(RedisClientImplTest, ConnectFail) {
  InSequence s;

  setup();

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_timer_, disableTimer());
  upstream_connection_->raiseEvents(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_connect_fail_.value());
  EXPECT_EQ(1UL, host_->stats_.cx_connect_fail_.value());
}

TEST_F(RedisClientImplTest, ConnectTimeout) {
  InSequence s;

  setup();

  RespValue request1;
  MockPoolCallbacks callbacks1;
  EXPECT_CALL(*encoder_, encode(Ref(request1), _));
  PoolRequest* handle1 = client_->makeRequest(request1, callbacks1);
  EXPECT_NE(nullptr, handle1);

  EXPECT_CALL(*upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(callbacks1, onFailure());
  EXPECT_CALL(*connect_timer_, disableTimer());
  connect_timer_->callback_();

  EXPECT_EQ(1UL, host_->cluster_.stats_.upstream_cx_connect_timeout_.value());
}

TEST(RedisClientFactoryImplTest, Basic) {
  ClientFactoryImpl factory;
  Upstream::MockHost::MockCreateConnectionData conn_info;
  conn_info.connection_ = new NiceMock<Network::MockClientConnection>();
  std::shared_ptr<Upstream::MockHost> host(new NiceMock<Upstream::MockHost>());
  EXPECT_CALL(*host, createConnection_(_)).WillOnce(Return(conn_info));
  NiceMock<Event::MockDispatcher> dispatcher;
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
