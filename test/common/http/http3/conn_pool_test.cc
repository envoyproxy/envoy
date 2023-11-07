#include <chrono>
#include <memory>

#include "source/common/http/http3/conn_pool.h"
#include "source/common/quic/quic_transport_socket_factory.h"

#include "test/common/http/common.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {
namespace Http3 {

class Http3ConnPoolImplPeer {
public:
  static std::list<Envoy::ConnectionPool::ActiveClientPtr>&
  connectingClients(Http3ConnPoolImpl& pool) {
    return pool.connecting_clients_;
  }
  static quic::QuicServerId& getServerId(Http3ConnPoolImpl& pool) { return pool.server_id_; }
};

class MockPoolConnectResultCallback : public PoolConnectResultCallback {
public:
  MOCK_METHOD(void, onHandshakeComplete, ());
  MOCK_METHOD(void, onZeroRttHandshakeFailed, ());
};

class Http3ConnPoolImplTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  Http3ConnPoolImplTest() {
    EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _))
        .WillRepeatedly(Return(ssl_context_));
    factory_.emplace(std::unique_ptr<Envoy::Ssl::ClientContextConfig>(
                         new NiceMock<Ssl::MockClientContextConfig>),
                     context_);
    factory_->initialize();
  }

  void initialize() {
    EXPECT_CALL(mockHost(), address()).WillRepeatedly(Return(test_address_));
    EXPECT_CALL(mockHost(), transportSocketFactory()).WillRepeatedly(testing::ReturnRef(*factory_));
    EXPECT_CALL(mockHost().cluster_, connectTimeout())
        .WillRepeatedly(Return(std::chrono::milliseconds(10000)));
    new Event::MockSchedulableCallback(&dispatcher_);
    Network::ConnectionSocket::OptionsSharedPtr options =
        std::make_shared<Network::Socket::Options>();
    options->push_back(socket_option_);
    ON_CALL(*mockHost().cluster_.upstream_local_address_selector_, getUpstreamLocalAddressImpl(_))
        .WillByDefault(Invoke(
            [](const Network::Address::InstanceConstSharedPtr&) -> Upstream::UpstreamLocalAddress {
              return Upstream::UpstreamLocalAddress({nullptr, nullptr});
            }));
    Network::TransportSocketOptionsConstSharedPtr transport_options;
    pool_ = allocateConnPool(
        dispatcher_, random_, host_, Upstream::ResourcePriority::Default, options,
        transport_options, state_, quic_stat_names_, {}, *store_.rootScope(),
        makeOptRef<PoolConnectResultCallback>(connect_result_callback_), quic_info_);
    EXPECT_EQ(3000, Http3ConnPoolImplPeer::getServerId(*pool_).port());
  }

  Upstream::MockHost& mockHost() { return static_cast<Upstream::MockHost&>(*host_); }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Quic::PersistentQuicInfoImpl quic_info_{dispatcher_, 45};
  Upstream::HostSharedPtr host_{new NiceMock<Upstream::MockHost>};
  NiceMock<Random::MockRandomGenerator> random_;
  Upstream::ClusterConnectivityState state_;
  Network::Address::InstanceConstSharedPtr test_address_ =
      Network::Utility::resolveUrl("tcp://127.0.0.1:3000");
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  absl::optional<Quic::QuicClientTransportSocketFactory> factory_;
  Ssl::ClientContextSharedPtr ssl_context_{new Ssl::MockClientContext()};
  Stats::IsolatedStoreImpl store_;
  Quic::QuicStatNames quic_stat_names_{store_.symbolTable()};
  std::unique_ptr<Http3ConnPoolImpl> pool_;
  MockPoolConnectResultCallback connect_result_callback_;
  std::shared_ptr<Network::MockSocketOption> socket_option_{new Network::MockSocketOption()};
};

class MockQuicClientTransportSocketFactory : public Quic::QuicClientTransportSocketFactory {
public:
  MockQuicClientTransportSocketFactory(
      Ssl::ClientContextConfigPtr config,
      Server::Configuration::TransportSocketFactoryContext& factory_context)
      : Quic::QuicClientTransportSocketFactory(std::move(config), factory_context) {}

  MOCK_METHOD(Envoy::Ssl::ClientContextSharedPtr, sslCtx, ());
};

TEST_F(Http3ConnPoolImplTest, FastFailWithoutSecretsLoaded) {
  MockQuicClientTransportSocketFactory factory{
      std::unique_ptr<Envoy::Ssl::ClientContextConfig>(new NiceMock<Ssl::MockClientContextConfig>),
      context_};

  EXPECT_CALL(factory, sslCtx()).WillRepeatedly(Return(nullptr));

  EXPECT_CALL(mockHost(), address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(mockHost(), transportSocketFactory()).WillRepeatedly(testing::ReturnRef(factory));
  // The unique pointer of this object will be returned in createSchedulableCallback_ of
  // dispatcher_, so there is no risk of object leak.
  new Event::MockSchedulableCallback(&dispatcher_);
  Network::ConnectionSocket::OptionsSharedPtr options;
  Network::TransportSocketOptionsConstSharedPtr transport_options;
  ConnectionPool::InstancePtr pool =
      allocateConnPool(dispatcher_, random_, host_, Upstream::ResourcePriority::Default, options,
                       transport_options, state_, quic_stat_names_, {}, *store_.rootScope(),
                       makeOptRef<PoolConnectResultCallback>(connect_result_callback_), quic_info_);

  EXPECT_EQ(static_cast<Http3ConnPoolImpl*>(pool.get())->instantiateActiveClient(), nullptr);
}

TEST_F(Http3ConnPoolImplTest, FailWithSecretsBecomeEmpty) {
  testing::NiceMock<Stats::MockStore> mock_store;
  ON_CALL(context_, statsScope()).WillByDefault(testing::ReturnRef(*mock_store.rootScope()));

  MockQuicClientTransportSocketFactory factory{
      std::unique_ptr<Envoy::Ssl::ClientContextConfig>(new NiceMock<Ssl::MockClientContextConfig>),
      context_};

  Ssl::ClientContextSharedPtr ssl_context(new Ssl::MockClientContext());
  EXPECT_CALL(factory, sslCtx())
      .WillOnce(Return(ssl_context))
      .WillOnce(Return(nullptr))
      .WillRepeatedly(Return(ssl_context));

  EXPECT_CALL(mockHost(), address()).WillRepeatedly(Return(test_address_));
  EXPECT_CALL(mockHost(), transportSocketFactory()).WillRepeatedly(testing::ReturnRef(factory));
  new Event::MockSchedulableCallback(&dispatcher_);
  Network::ConnectionSocket::OptionsSharedPtr options;
  Network::TransportSocketOptionsConstSharedPtr transport_options;
  ConnectionPool::InstancePtr pool =
      allocateConnPool(dispatcher_, random_, host_, Upstream::ResourcePriority::Default, options,
                       transport_options, state_, quic_stat_names_, {}, *store_.rootScope(),
                       makeOptRef<PoolConnectResultCallback>(connect_result_callback_), quic_info_);

  MockResponseDecoder decoder;
  ConnPoolCallbacks callbacks;
  EXPECT_CALL(mock_store.counter_, inc());
  EXPECT_CALL(callbacks.pool_failure_, ready());
  EXPECT_EQ(pool->newStream(decoder, callbacks,
                            {/*can_send_early_data_=*/false,
                             /*can_use_http3_=*/true}),
            nullptr);
}

TEST_F(Http3ConnPoolImplTest, CreationAndNewStream) {
  initialize();

  MockResponseDecoder decoder;
  ConnPoolCallbacks callbacks;
  mockHost().cluster_.cluster_socket_options_ = std::make_shared<Network::Socket::Options>();
  std::shared_ptr<Network::MockSocketOption> cluster_socket_option{new Network::MockSocketOption()};
  mockHost().cluster_.cluster_socket_options_->push_back(cluster_socket_option);
  EXPECT_CALL(*mockHost().cluster_.upstream_local_address_selector_, getUpstreamLocalAddressImpl(_))
      .WillOnce(Invoke(
          [&](const Network::Address::InstanceConstSharedPtr&) -> Upstream::UpstreamLocalAddress {
            Network::ConnectionSocket::OptionsSharedPtr options =
                std::make_shared<Network::ConnectionSocket::Options>();
            Network::Socket::appendOptions(options, mockHost().cluster_.cluster_socket_options_);
            return Upstream::UpstreamLocalAddress({nullptr, options});
          }));
  EXPECT_CALL(*cluster_socket_option, setOption(_, _)).Times(3u);
  EXPECT_CALL(*socket_option_, setOption(_, _)).Times(3u);
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  auto* async_connect_callback = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  ConnectionPool::Cancellable* cancellable = pool_->newStream(decoder, callbacks,
                                                              {/*can_send_early_data_=*/false,
                                                               /*can_use_http3_=*/true});
  EXPECT_NE(nullptr, cancellable);
  async_connect_callback->invokeCallback();

  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  std::list<Envoy::ConnectionPool::ActiveClientPtr>& clients =
      Http3ConnPoolImplPeer::connectingClients(*pool_);
  EXPECT_EQ(1u, clients.size());
  EXPECT_CALL(connect_result_callback_, onHandshakeComplete()).WillOnce(Invoke([cancellable]() {
    cancellable->cancel(Envoy::ConnectionPool::CancelPolicy::Default);
  }));
  pool_->onConnectionEvent(*clients.front(), "", Network::ConnectionEvent::Connected);
}

TEST_F(Http3ConnPoolImplTest, NewAndCancelStreamBeforeConnect) {
  initialize();

  MockResponseDecoder decoder;
  ConnPoolCallbacks callbacks;
  EXPECT_CALL(*socket_option_, setOption(_, _)).Times(2u);
  auto* async_connect_callback = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  ConnectionPool::Cancellable* cancellable = pool_->newStream(decoder, callbacks,
                                                              {/*can_send_early_data_=*/false,
                                                               /*can_use_http3_=*/true});
  EXPECT_NE(nullptr, cancellable);
  std::list<Envoy::ConnectionPool::ActiveClientPtr>& clients =
      Http3ConnPoolImplPeer::connectingClients(*pool_);
  EXPECT_EQ(1u, clients.size());
  Envoy::ConnectionPool::ActiveClient& client_ref = *clients.front();

  // Cancel the stream before async connect.
  EXPECT_CALL(dispatcher_, deferredDelete_(_));
  cancellable->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
  EXPECT_TRUE(clients.empty());
  EXPECT_EQ(Envoy::ConnectionPool::ActiveClient::State::Closed, client_ref.state());
  EXPECT_EQ(dispatcher_.to_delete_.front().get(), &client_ref);

  EXPECT_CALL(*async_connect_callback, cancel());
  dispatcher_.to_delete_.clear();
}

TEST_F(Http3ConnPoolImplTest, NewAndDrainClientBeforeConnect) {
  initialize();

  MockResponseDecoder decoder;
  ConnPoolCallbacks callbacks;
  EXPECT_CALL(*socket_option_, setOption(_, _)).Times(3u);
  auto* async_connect_callback = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  ConnectionPool::Cancellable* cancellable = pool_->newStream(decoder, callbacks,
                                                              {/*can_send_early_data_=*/false,
                                                               /*can_use_http3_=*/true});
  EXPECT_NE(nullptr, cancellable);
  std::list<Envoy::ConnectionPool::ActiveClientPtr>& clients =
      Http3ConnPoolImplPeer::connectingClients(*pool_);
  EXPECT_EQ(1u, clients.size());

  pool_->drainConnectionsImpl(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);

  // Triggering the async connect callback after the client starts draining shouldn't cause crash.
  async_connect_callback->invokeCallback();
  cancellable->cancel(Envoy::ConnectionPool::CancelPolicy::CloseExcess);
}

} // namespace Http3
} // namespace Http
} // namespace Envoy
