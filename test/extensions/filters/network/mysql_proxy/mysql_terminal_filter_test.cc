#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/conn_pool.h"
#include "envoy/common/exception.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_config.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_session.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_terminal_filter.h"
#include "source/extensions/filters/network/mysql_proxy/mysql_utils.h"
#include "source/extensions/filters/network/mysql_proxy/route.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

const std::string cluster_name = "cluster";

envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy
parseProtoFromYaml(const std::string& yaml_string) {
  envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy config;
  TestUtility::loadFromYaml(yaml_string, config);
  return config;
}

class MySQLTerminalFitlerTest
    : public DecoderFactory,
      public testing::TestWithParam<Tcp::ConnectionPool::PoolFailureReason> {
public:
  const std::string yaml_string = R"EOF(
  database_routes:
    catch_all_route:
      database: db
      cluster: cluster
  stat_prefix: foo
  )EOF";

  struct DecoderStatus {
    int seq;
    MySQLSession::State state;
  };

  DecoderPtr create(DecoderCallbacks&) override { return DecoderPtr{decoder_}; }

  void SetUp() override {
    {
      auto proto_config = parseProtoFromYaml(yaml_string);
      MySQLFilterConfigSharedPtr config = std::make_shared<MySQLFilterConfig>("mysql", store_);
      route_ = std::make_shared<MockRoute>(&cm_.thread_local_cluster_, cluster_name);
      router_ = std::make_shared<MockRouter>(route_);
      filter_ = std::make_unique<MySQLTerminalFilter>(config, router_, *this);

      EXPECT_CALL(read_callbacks_, connection());
      EXPECT_CALL(read_callbacks_.connection_, addConnectionCallbacks(_));
      filter_->initializeReadFilterCallbacks(read_callbacks_);
    }
  }

  void TearDown() override {
    {
      filter_.reset(nullptr);
      router_.reset();
      route_.reset();
    }
  }

  void connectionComeNoDefaultClusterInConfig() {
    auto router = std::make_shared<MockRouter>(nullptr);
    filter_->router_ = router;
    EXPECT_CALL(*router, defaultPool());
    EXPECT_CALL(read_callbacks_, connection());
    EXPECT_CALL(read_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
    EXPECT_CALL(store_.counter_, inc);

    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  }

  void connectionComeClusterNotExisit() {
    auto route = std::make_shared<MockRoute>(nullptr, cluster_name);
    auto router = std::make_shared<MockRouter>(route);

    filter_->router_ = router;
    EXPECT_CALL(*router, defaultPool());
    EXPECT_CALL(*route, upstream());

    EXPECT_CALL(read_callbacks_, connection());
    EXPECT_CALL(read_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
    EXPECT_CALL(store_.counter_, inc);

    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  }

  void connectionComeNoHost() {
    auto route = std::make_shared<MockRoute>(&cm_.thread_local_cluster_, cluster_name);
    auto router = std::make_shared<MockRouter>(route);
    filter_->router_ = router;
    EXPECT_CALL(*router, defaultPool());
    EXPECT_CALL(*route, upstream());

    EXPECT_CALL(cm_.thread_local_cluster_,
                tcpConnPool(Upstream::ResourcePriority::Default, nullptr))
        .WillOnce(Invoke([&](Upstream::ResourcePriority, Upstream::LoadBalancerContext*)
                             -> absl::optional<Envoy::Upstream::TcpPoolData> {
          return absl::optional<Envoy::Upstream::TcpPoolData>();
        }));

    EXPECT_CALL(read_callbacks_, connection());
    EXPECT_CALL(read_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));

    EXPECT_CALL(store_.counter_, inc);

    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  }

  void connectionOk() {
    auto route = std::make_shared<MockRoute>(&cm_.thread_local_cluster_, cluster_name);
    auto router = std::make_shared<MockRouter>(route);
    filter_->router_ = router;
    EXPECT_CALL(*router, defaultPool());
    EXPECT_CALL(*route, upstream());

    EXPECT_CALL(cm_.thread_local_cluster_,
                tcpConnPool(Upstream::ResourcePriority::Default, nullptr));

    EXPECT_CALL(cm_.thread_local_cluster_.tcp_conn_pool_, newConnection(_));
    EXPECT_CALL(store_.counter_, inc);
    EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  }

  void connectClosedAndPoolNotReady() {
    connectionOk();
    EXPECT_CALL(*(dynamic_cast<Envoy::ConnectionPool::MockCancellable*>(filter_->canceler_)),
                cancel(_));
    EXPECT_EQ(filter_->upstream_conn_data_, nullptr);
    read_callbacks_.connection_.close(Network::ConnectionCloseType::NoFlush);
    EXPECT_EQ(filter_->canceler_, nullptr);
  }

  MySQLTerminalFilter::UpstreamEventHandlerPtr& upstreamEventHandler() {
    return filter_->upstream_event_handler_;
  }

  void connectOkAndPoolReady() {
    connectionOk();
    EXPECT_CALL(*(cm_.thread_local_cluster_.tcp_conn_pool_.connection_data_.get()),
                addUpstreamCallbacks(_));
    EXPECT_CALL(read_callbacks_, continueReading());
    cm_.thread_local_cluster_.tcp_conn_pool_.poolReady(connection_);

    EXPECT_NE(upstreamConnData(), nullptr);
  }
  void exec(std::function<void()> fn, bool remote_write = false, bool local_write = false,
            bool close = false);
  // seq 0
  void serverSendGreet();
  // seq 1
  void clientSendSsl();
  void clientSendHandShakeReponse();
  // seq 2
  void serverSendOk();
  void serverSendError();
  void serverSendAuthSwitch();
  void serverSendAuthMore();
  // seq 3
  void clientSendAuthSwitchResponse();
  // seq 4
  void serverSendClientSwitchOk();
  void serverSendClientSwitchError();
  void serverSendClientSwitchMore();
  void serverSendClientSwitchSwitch();
  void clientSendAuthMoreResponse();

  // command phase seq 0
  void clientSendQuery();
  void clientSendQuit();
  void clientSendInvalidQuery();

  // command phase seq 1 - N
  void serverSendCommandResponse();

  Tcp::ConnectionPool::MockConnectionData* upstreamConnData() {
    return dynamic_cast<Tcp::ConnectionPool::MockConnectionData*>(
        filter_->upstream_conn_data_.get());
  }
  Upstream::MockClusterManager cm_;
  Network::MockClientConnection connection_;
  Network::MockReadFilterCallbacks read_callbacks_;
  MockDecoder* decoder_{new MockDecoder()};
  RouteSharedPtr route_;
  RouterSharedPtr router_;
  MySQLTerminalFilterPtr filter_;
  NiceMock<Stats::MockStore> store_;
};

INSTANTIATE_TEST_CASE_P(ConnectOkButPoolFailed, MySQLTerminalFitlerTest,
                        ::testing::ValuesIn({
                            Tcp::ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                            Tcp::ConnectionPool::PoolFailureReason::RemoteConnectionFailure,
                            Tcp::ConnectionPool::PoolFailureReason::Timeout,
                            Tcp::ConnectionPool::PoolFailureReason::Overflow,
                            static_cast<Tcp::ConnectionPool::PoolFailureReason>(-1),
                        }));

void MySQLTerminalFitlerTest::exec(std::function<void()> fn, bool remote_write, bool local_write,
                                   bool close) {
  EXPECT_CALL(read_callbacks_, connection()).Times(local_write + close);
  if (local_write) {
    EXPECT_CALL(read_callbacks_.connection_, write(_, false));
  }
  if (remote_write) {
    EXPECT_CALL(connection_, write(_, false));
  }

  if (close) {
    EXPECT_CALL(read_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
    EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  }
  fn();
}

void MySQLTerminalFitlerTest::serverSendGreet() {
  auto greet = MessageHelper::encodeGreeting(MySQLTestUtils::getAuthPluginData8());
  greet.setSeq(0);
  auto data = greet.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), greet.encodePacket().toString());
    data.drain(data.length());
    exec([&]() { filter_->onServerGreeting(greet); }, false, true, false);
  }));

  filter_->onData(data, false);
}

void MySQLTerminalFitlerTest::clientSendSsl() {
  auto ssl = MessageHelper::encodeSslUpgrade();
  ssl.setSeq(1);
  auto data = ssl.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), ssl.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(store_.counter_, inc());
          filter_->onClientLogin(ssl);
        },
        false, false, true);
  }));
  filter_->onData(data, false);
}

void MySQLTerminalFitlerTest::clientSendHandShakeReponse() {
  auto login = MessageHelper::encodeClientLogin(
      MySQLTestUtils::getUsername(), MySQLTestUtils::getDb(), MySQLTestUtils::getAuthResp20());
  login.setSeq(1);
  auto data = login.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), login.encodePacket().toString());
    data.drain(data.length());
    exec([&]() { filter_->onClientLogin(login); }, true, false, false);
  }));
  filter_->onData(data, false);
}

void MySQLTerminalFitlerTest::serverSendOk() {
  auto ok = MessageHelper::encodeOk();
  ok.setSeq(2);
  auto data = ok.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), ok.encodePacket().toString());
    data.drain(data.length());
    exec([&]() { filter_->onClientLoginResponse(ok); }, false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::serverSendError() {
  auto err = MessageHelper::passwordLengthError(NATIVE_PSSWORD_HASH_LENGTH);
  err.setSeq(2);
  auto data = err.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), err.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(store_.counter_, inc());
          filter_->onClientLoginResponse(err);
          ;
        },
        false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::serverSendAuthSwitch() {
  testing::Sequence seq;
  auto auth_switch = MessageHelper::encodeAuthSwitch(MySQLTestUtils::getAuthPluginData8());
  auth_switch.setSeq(2);
  auto data = auth_switch.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    testing::Sequence seq;
    EXPECT_EQ(data.toString(), auth_switch.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(store_.counter_, inc());
          filter_->onClientLoginResponse(auth_switch);
          ;
        },
        false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::serverSendAuthMore() {
  SCOPED_TRACE("");

  testing::Sequence seq;

  auto more = MessageHelper::encodeAuthMore(MySQLTestUtils::getAuthPluginData8());
  more.setSeq(2);
  auto data = more.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), more.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          filter_->onClientLoginResponse(more);
          ;
        },
        false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::clientSendAuthSwitchResponse() {
  testing::Sequence seq;

  auto switch_resp = MessageHelper::encodeSwithResponse(MySQLTestUtils::getAuthResp20());
  switch_resp.setSeq(3);
  auto data = switch_resp.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), switch_resp.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          filter_->onClientSwitchResponse(switch_resp);
          ;
        },
        true, false, false);
  }));
  filter_->onData(data, false);
}

// seq 4
void MySQLTerminalFitlerTest::serverSendClientSwitchOk() {
  testing::Sequence seq;

  auto ok = MessageHelper::encodeOk();
  ok.setSeq(4);
  auto data = ok.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), ok.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          filter_->onMoreClientLoginResponse(ok);
          ;
        },
        false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::serverSendClientSwitchError() {
  testing::Sequence seq;
  auto err = MessageHelper::passwordLengthError(NATIVE_PSSWORD_HASH_LENGTH);
  err.setSeq(4);
  auto data = err.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), err.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(store_.counter_, inc());
          filter_->onMoreClientLoginResponse(err);
        },
        false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::serverSendClientSwitchMore() {
  testing::Sequence seq;
  auto more = MessageHelper::encodeAuthMore(MySQLTestUtils::getAuthPluginData8());
  more.setSeq(4);
  auto data = more.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), more.encodePacket().toString());
    data.drain(data.length());
    exec([&]() { filter_->onMoreClientLoginResponse(more); }, false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

void MySQLTerminalFitlerTest::serverSendClientSwitchSwitch() {
  testing::Sequence seq;
  auto auth_switch = MessageHelper::encodeAuthSwitch(MySQLTestUtils::getAuthPluginData8());
  auth_switch.setSeq(4);
  auto data = auth_switch.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    testing::Sequence seq;
    EXPECT_EQ(data.toString(), auth_switch.encodePacket().toString());
    data.drain(data.length());
    exec([&]() { filter_->onMoreClientLoginResponse(auth_switch); }, false, true, false);
  }));
  upstreamEventHandler()->onUpstreamData(data, false);
}

// command phase seq 0
void MySQLTerminalFitlerTest::clientSendQuery() {
  auto command = MessageHelper::encodeCommand(Command::Cmd::Query, "select * from t;", "", true);
  command.setSeq(0);
  auto data = command.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), command.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(read_callbacks_, connection()).Times(1 + 1);
          EXPECT_CALL(store_.counter_, inc());
          filter_->onCommand(command);
        },
        true, false, false);
  }));
  filter_->onData(data, false);
}

void MySQLTerminalFitlerTest::clientSendInvalidQuery() {
  auto command = MessageHelper::encodeCommand(Command::Cmd::Query, "slecet * from t;", "", true);
  command.setSeq(0);
  auto data = command.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), command.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(read_callbacks_, connection());
          EXPECT_CALL(store_.counter_, inc());
          filter_->onCommand(command);
        },
        true, false, false);
  }));
  filter_->onData(data, false);
}

void MySQLTerminalFitlerTest::clientSendQuit() {
  auto quit = MessageHelper::encodeCommand(Command::Cmd::Quit, "", "", false);
  quit.setSeq(0);
  auto data = quit.encodePacket();
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), quit.encodePacket().toString());
    data.drain(data.length());
    exec([&]() { filter_->onCommand(quit); }, true, false, true);
  }));
  filter_->onData(data, false);
}

void MySQLTerminalFitlerTest::serverSendCommandResponse() {
  auto response = MessageHelper::encodeCommandResponse("command response");
  response.setSeq(1);
  auto data = response.encodePacket();

  EXPECT_CALL(*decoder_, onData(_)).WillOnce(Invoke([&](Buffer::Instance& data) {
    EXPECT_EQ(data.toString(), response.encodePacket().toString());
    data.drain(data.length());
    exec(
        [&]() {
          EXPECT_CALL(*decoder_, getSession()).Times(2);
          filter_->onCommandResponse(response);
        },
        false, true, false);
  }));

  upstreamEventHandler()->onUpstreamData(data, false);
}

TEST_F(MySQLTerminalFitlerTest, ConnectButNoClusterInRoute) {
  connectionComeNoDefaultClusterInConfig();
}

TEST_F(MySQLTerminalFitlerTest, ConnectButNoCluster) { connectionComeClusterNotExisit(); }

TEST_F(MySQLTerminalFitlerTest, ConnectButNoHost) { connectionComeNoHost(); }

TEST_F(MySQLTerminalFitlerTest, ConnectOk) { connectionOk(); }

TEST_F(MySQLTerminalFitlerTest, ConnectClosedAndPoolNotReady) { connectClosedAndPoolNotReady(); }

TEST_P(MySQLTerminalFitlerTest, ConnectOkButPoolFailed) {
  connectionOk();
  EXPECT_CALL(store_.counter_, inc);
  EXPECT_CALL(read_callbacks_, connection());
  EXPECT_CALL(read_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  cm_.thread_local_cluster_.tcp_conn_pool_.poolFailure(GetParam());
}

TEST_F(MySQLTerminalFitlerTest, ConnectOkAndPoolReady) { connectOkAndPoolReady(); }

TEST_F(MySQLTerminalFitlerTest, GreetThenSslUpgrade) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendSsl();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenResponse) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenOk) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendOk();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenError) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendError();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenAuthSwitchThenError) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendAuthSwitch();
  clientSendAuthSwitchResponse();
  serverSendClientSwitchError();
}

// pending
TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenAuthSwitchThenMore) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendAuthSwitch();
  clientSendAuthSwitchResponse();
  serverSendClientSwitchMore();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenAuthSwitchThenSwitch) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendAuthSwitch();
  clientSendAuthSwitchResponse();
  serverSendClientSwitchSwitch();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenAuthSwitchThenOkThenQueryThenResult) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendAuthSwitch();
  clientSendAuthSwitchResponse();
  serverSendClientSwitchOk();
  clientSendQuery();
  serverSendCommandResponse();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenAuthSwitchThenOkThenQuit) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendAuthSwitch();
  clientSendAuthSwitchResponse();
  serverSendClientSwitchOk();
  clientSendQuit();
}

TEST_F(MySQLTerminalFitlerTest, GreetThenLoginThenAuthSwitchThenOkThenInvalidQuery) {
  connectOkAndPoolReady();
  serverSendGreet();
  clientSendHandShakeReponse();
  serverSendAuthSwitch();
  clientSendAuthSwitchResponse();
  serverSendClientSwitchOk();
  clientSendInvalidQuery();
}
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
