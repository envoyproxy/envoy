

#include "envoy/upstream/resource_manager.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/conn_pool_impl.h"
#include "extensions/filters/network/mysql_proxy/message_helper.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_client_impl.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"

#include "gtest/gtest.h"
#include "mock.h"
#include "mysql_test_utils.h"

using testing::NiceMock;
using testing::Sequence;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnectionPool {
constexpr int START_CONNECTION = 0;
constexpr int MAX_IDLE_CONNECTION = 1;
constexpr int MAX_CONNECTION = 2;

class ConnPoolTest : public testing::Test, public DecoderFactory {
public:
  ConnPoolTest()
      : config_("fake_db", "fake_cluster", MAX_CONNECTION, MAX_IDLE_CONNECTION, START_CONNECTION),
        upstream_connections_(MAX_CONNECTION) {
    ON_CALL(*this, create).WillByDefault(Invoke([&](DecoderCallbacks& callbacks) -> DecoderPtr {
      return this->create_(callbacks);
    }));
  }
  MOCK_METHOD((DecoderPtr), create, (DecoderCallbacks&));

  DecoderPtr create_(DecoderCallbacks& callbacks) {
    upstream_decoder_callbacks_.push_back(&callbacks);
    upstream_decoders_.push_back(new MockDecoder(session_));
    return DecoderPtr{upstream_decoders_.back()};
  }

  void setup(size_t start_connections = 0, size_t totol_connections = 0) {
    EXPECT_CALL(context_.thread_local_, allocateSlot());
    EXPECT_CALL(*this, create).Times(totol_connections);
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_,
                tcpConnPool(Upstream::ResourcePriority::Default, nullptr));
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
        .Times(totol_connections);
    for (size_t i = 0; i < start_connections; i++) {
      EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                  addUpstreamCallbacks(_))
          .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
            upstream_clients_.push_back(dynamic_cast<InstanceImpl::ThreadLocalActiveClient*>(&cb));
            ;
          }));
    }
    context_.cluster_manager_.initializeThreadLocalClusters(
        std::vector<std::string>{config_.cluster});
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster);
    conn_pool_ = std::make_shared<InstanceImpl>(context_.thread_local_, &context_.cluster_manager_);

    ConnectionPoolSettings setting(config_);
    setting.start_connections = start_connections;
    conn_pool_->init(&context_.cluster_manager_, *this, setting, auth_username_, auth_password_);
    checkSize(0, start_connections, 0, 0);

    for (size_t i = 0; i < start_connections; i++) {
      context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
          upstream_connections_[i]);
    }

    for (size_t i = 0; i < start_connections; i++) {
      EXPECT_CALL(*upstream_decoders_[i], getSession());
      EXPECT_CALL(upstream_connections_[i], write(_, false));
      auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
      upstream_clients_[i]->onServerGreeting(greet);
      auto ok = MessageHelper::encodeOk();
      upstream_clients_[i]->onClientLoginResponse(ok);
      EXPECT_EQ(upstream_clients_[i]->state_, InstanceImpl::ClientState::Ready);
    }
    checkSize(0, 0, start_connections, 0);
  }
  void teardown() {
    tls_.shutdownThread();
    conn_pool_.reset();
    upstream_decoders_.clear();
    upstream_decoder_callbacks_.clear();
    upstream_clients_.clear();
  }

  void checkSize(int pending_requests_size, int pending_clients_size, int active_clients_size,
                 int busy_clients_size) {
    EXPECT_EQ(pendingRequests().size(), pending_requests_size);
    EXPECT_EQ(pendingClients().size(), pending_clients_size);
    EXPECT_EQ(activeClients().size(), active_clients_size);
    EXPECT_EQ(busyClients().size(), busy_clients_size);
  }

  struct ClientData {
    MockDecoder* decoder{nullptr};
    DecoderCallbacks* callbacks{nullptr};
    std::unique_ptr<NiceMock<Network::MockClientConnection>> connection{nullptr};
    std::unique_ptr<MockClientPoolCallbacks> client_callbacks{nullptr};
    InstanceImpl::ThreadLocalActiveClient* client{nullptr};
  };

  void prepareDeocderCallbacks(ClientData& data, bool will_create_again = false) {
    Sequence seq;
    EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
        .Times(1 + will_create_again);
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks)
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& callback) {
          data.client = dynamic_cast<InstanceImpl::ThreadLocalActiveClient*>(&callback);
        }));

    data.decoder = new MockDecoder(session_);
    data.client_callbacks = std::make_unique<MockClientPoolCallbacks>();
    EXPECT_CALL(*this, create).WillOnce(Invoke([&](DecoderCallbacks& cb) -> DecoderPtr {
      data.callbacks = &cb;
      return DecoderPtr{data.decoder};
    }));

    auto* cancel = conn_pool_->newMySQLClient(*data.client_callbacks);
    EXPECT_NE(cancel, nullptr);
    checkSize(1, 1, 0, 0);

    data.connection = std::make_unique<NiceMock<Network::MockClientConnection>>();
    context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(*data.connection);
  }

  std::list<InstanceImpl::ThreadLocalActiveClientPtr>& pendingClients() {
    return conn_pool_->tls_->getTyped<InstanceImpl::ThreadLocalClientPool>().pending_clients_;
  }
  std::list<InstanceImpl::ThreadLocalActiveClientPtr>& busyClients() {
    return conn_pool_->tls_->getTyped<InstanceImpl::ThreadLocalClientPool>().busy_clients_;
  }
  std::list<InstanceImpl::ThreadLocalActiveClientPtr>& activeClients() {
    return conn_pool_->tls_->getTyped<InstanceImpl::ThreadLocalClientPool>().active_clients_;
  }
  std::list<InstanceImpl::PendingRequestPtr>& pendingRequests() {
    return conn_pool_->tls_->getTyped<InstanceImpl::ThreadLocalClientPool>().pending_requests_;
  }

  MySQLSession session_;
  DecoderCallbacks* decoder_callbacks_{nullptr};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<InstanceImpl> conn_pool_;
  MockClient* client_{};
  Network::Address::InstanceConstSharedPtr test_address_;
  std::string auth_username_;
  std::string auth_password_;
  NiceMock<Api::MockApi> api_;
  ConnectionPoolSettings config_;
  std::vector<DecoderCallbacks*> upstream_decoder_callbacks_;
  std::vector<MockDecoder*> upstream_decoders_;
  std::vector<InstanceImpl::ThreadLocalActiveClient*> upstream_clients_;
  std::vector<NiceMock<Network::MockClientConnection>> upstream_connections_;
};

TEST_F(ConnPoolTest, BasicFailPoolClient) {
  setup();
  Sequence seq;
  auto client_callbacks = std::make_unique<MockClientPoolCallbacks>();
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_));
  EXPECT_CALL(*client_callbacks, onClientFailure(MySQLPoolFailureReason::RemoteConnectionFailure));

  auto* cancel = conn_pool_->newMySQLClient(*client_callbacks);
  EXPECT_NE(cancel, nullptr);
  EXPECT_EQ(pendingRequests().size(), 1);
  EXPECT_EQ(pendingClients().size(), 1);
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      PoolFailureReason::RemoteConnectionFailure);
  teardown();
}

TEST_F(ConnPoolTest, FailByProtocolErr) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);
  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));
  client_data.callbacks->onProtocolError();
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, FailByNotHandledErrAtClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);
  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));
  EXPECT_CALL(*client_data.decoder, getSession());
  EXPECT_CALL(*client_data.connection, write(_, false));
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto switch_more = MessageHelper::encodeAuthMore(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onClientLoginResponse(switch_more);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, FailByAuthErrAtClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);
  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);

        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::AuthFailure));
  EXPECT_CALL(*client_data.decoder, getSession());
  EXPECT_CALL(*client_data.connection, write(_, false));
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto err = MessageHelper::passwordLengthError(10);
  client_data.callbacks->onClientLoginResponse(err);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, FailByNotSupportPluginErrAtClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::AuthFailure));
  EXPECT_CALL(*client_data.decoder, getSession());
  EXPECT_CALL(*client_data.connection, write(_, false));
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto auth_switch = MessageHelper::encodeAuthSwitch(MySQLTestUtils::getAuthPluginData20(),
                                                     "msyql_unknown_password");
  client_data.callbacks->onClientLoginResponse(auth_switch);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, OkAtClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.decoder, getSession());
  EXPECT_CALL(*client_data.connection, write(_, false));
  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Busy);

        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientReady_(_));

  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto ok = MessageHelper::encodeOk();
  client_data.callbacks->onClientLoginResponse(ok);
  EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Busy);

  checkSize(0, 0, 0, 1);
  teardown();
}

TEST_F(ConnPoolTest, FailByNotHandledErrAtMoreClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);
  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));

  EXPECT_CALL(*client_data.decoder, getSession()).Times(2);
  EXPECT_CALL(*client_data.connection, write(_, false)).Times(2);

  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto auth_switch = MessageHelper::encodeAuthSwitch(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onClientLoginResponse(auth_switch);

  client_data.callbacks->onMoreClientLoginResponse(auth_switch);

  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, FailByAuthErrAtMoreClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);

        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::AuthFailure));

  EXPECT_CALL(*client_data.decoder, getSession()).Times(2);
  EXPECT_CALL(*client_data.connection, write(_, false)).Times(2);
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto auth_switch = MessageHelper::encodeAuthSwitch(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onClientLoginResponse(auth_switch);

  auto err = MessageHelper::passwordLengthError(10);
  client_data.callbacks->onMoreClientLoginResponse(err);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, OkAtMoreClientLoginResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data, false);

  EXPECT_CALL(*client_data.decoder, getSession()).Times(2);
  EXPECT_CALL(*client_data.connection, write(_, false)).Times(2);
  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Busy);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientReady_(_));
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onServerGreeting(greet);

  auto auth_switch = MessageHelper::encodeAuthSwitch(MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onClientLoginResponse(auth_switch);

  auto ok = MessageHelper::encodeOk();
  client_data.callbacks->onMoreClientLoginResponse(ok);
  checkSize(0, 0, 0, 1);
  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnClientLogin) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));

  auto client_login = MessageHelper::encodeClientLogin(AuthMethod::NativePassword, "user", "pass",
                                                       "db", MySQLTestUtils::getAuthPluginData20());
  client_data.callbacks->onClientLogin(client_login);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnSwithResponse) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);
        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));

  auto switch_resp = MessageHelper::encodeSwithResponse(MySQLTestUtils::getAuthResp20());
  client_data.callbacks->onClientSwitchResponse(switch_resp);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnCommand) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);

        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));

  auto command = MessageHelper::encodeCommand(Command::Cmd::InitDb, "", "db", false);
  client_data.callbacks->onCommand(command);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, ImpossibleDeocderCallbackOnCommandResp) {
  setup();
  Sequence seq;
  ClientData client_data;
  prepareDeocderCallbacks(client_data);

  EXPECT_CALL(*client_data.connection, close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(client_data.client->state_, InstanceImpl::ClientState::Uinit);

        client_data.client->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(*client_data.client_callbacks, onClientFailure(MySQLPoolFailureReason::ParseFailure));

  auto command_resp = MessageHelper::encodeCommandResponse("data");
  client_data.callbacks->onCommandResponse(command_resp);
  checkSize(0, 0, 0, 0);
  teardown();
}

TEST_F(ConnPoolTest, GetClientFromStartClients) {
  setup(1, 1);
  Sequence seq;

  auto client_callbacks = std::make_unique<MockClientPoolCallbacks>();
  EXPECT_CALL(*client_callbacks, onClientReady_(_));
  EXPECT_CALL(upstream_connections_[0], close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(upstream_clients_[0]->state_, InstanceImpl::ClientState::Busy);
        upstream_clients_[0]->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  auto canceler = conn_pool_->newMySQLClient(*client_callbacks);
  checkSize(0, 0, 0, 1);
  EXPECT_EQ(canceler, nullptr);
  EXPECT_NE(upstream_clients_[0], nullptr);
  EXPECT_EQ(upstream_clients_[0]->state_, InstanceImpl::ClientState::Busy);

  teardown();
}

TEST_F(ConnPoolTest, CloseClient) {
  setup(1, 1);
  Sequence seq;

  auto client_callbacks = std::make_unique<MockClientPoolCallbacks>();
  EXPECT_CALL(*client_callbacks, onClientReady_(_));
  EXPECT_CALL(upstream_connections_[0], close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        EXPECT_EQ(upstream_clients_[0]->state_, InstanceImpl::ClientState::Ready);

        upstream_clients_[0]->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  auto canceler = conn_pool_->newMySQLClient(*client_callbacks);
  EXPECT_EQ(canceler, nullptr);
  EXPECT_NE(upstream_clients_[0], nullptr);
  EXPECT_EQ(upstream_clients_[0]->state_, InstanceImpl::ClientState::Busy);
  client_callbacks->data_->close();
  EXPECT_EQ(upstream_clients_[0]->state_, InstanceImpl::ClientState::Ready);
  checkSize(0, 0, 1, 0);
  teardown();
}

TEST_F(ConnPoolTest, CloseMoreThanMaxIdleClients) {
  setup(1, 2);
  Sequence seq;

  auto client_callbacks1 = std::make_unique<MockClientPoolCallbacks>();
  EXPECT_CALL(*client_callbacks1, onClientReady_(_));

  // get from start connection
  auto canceler = conn_pool_->newMySQLClient(*client_callbacks1);
  EXPECT_EQ(canceler, nullptr);
  EXPECT_NE(upstream_clients_[0], nullptr);
  EXPECT_EQ(upstream_clients_[0]->state_, InstanceImpl::ClientState::Busy);

  checkSize(0, 0, 0, 1);

  // new create
  auto client_callbacks2 = std::make_unique<MockClientPoolCallbacks>();
  EXPECT_CALL(*client_callbacks2, onClientReady_(_));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
        upstream_clients_.push_back(dynamic_cast<InstanceImpl::ThreadLocalActiveClient*>(&cb));
        ;
      }));
  EXPECT_CALL(upstream_connections_[0], close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        upstream_clients_[0]->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  EXPECT_CALL(upstream_connections_[1], close(_))
      .WillOnce(Invoke([&](Network::ConnectionCloseType type) {
        EXPECT_EQ(Network::ConnectionCloseType::NoFlush, type);
        upstream_clients_[1]->onEvent(Network::ConnectionEvent::LocalClose);
      }));
  canceler = conn_pool_->newMySQLClient(*client_callbacks2);
  EXPECT_NE(canceler, nullptr);
  checkSize(1, 1, 0, 1);

  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
      upstream_connections_[1]);
  checkSize(1, 1, 0, 1);

  EXPECT_CALL(*upstream_decoders_[1], getSession());
  EXPECT_CALL(upstream_connections_[1], write(_, false));
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData20());
  upstream_clients_[1]->onServerGreeting(greet);
  auto ok = MessageHelper::encodeOk();
  upstream_clients_[1]->onClientLoginResponse(ok);
  EXPECT_EQ(upstream_clients_[1]->state_, InstanceImpl::ClientState::Busy);

  checkSize(0, 0, 0, 2);
  client_callbacks1->data_->close();
  checkSize(0, 0, 1, 1);
  client_callbacks2->data_->close();
  checkSize(0, 0, 1, 0);

  teardown();
}

} // namespace ConnectionPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy