#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/conn_pool.h"
#include "extensions/filters/network/mysql_proxy/message_helper.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_config.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/tcp/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy
parseProtoFromYaml(const std::string& yaml_string) {
  envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy config;
  TestUtility::loadFromYaml(yaml_string, config);
  return config;
}

class MySQLProxyFilterConfigTest : public testing::Test {
public:
  Stats::TestUtil::TestStore store_;
  NiceMock<Api::MockApi> api_;
};

TEST_F(MySQLProxyFilterConfigTest, Normal) {
  const std::string yaml_string = R"EOF(
  routes:
  - database: db
    cluster: cluster
  stat_prefix: foo
  )EOF";

  auto proto_config = parseProtoFromYaml(yaml_string);
  MySQLFilterConfig config(store_, proto_config, api_);
  EXPECT_TRUE(config.username_.empty());
  EXPECT_TRUE(config.password_.empty());
}

TEST_F(MySQLProxyFilterConfigTest, Bad) {
  const std::string yaml_string = R"EOF(
  routes:
  downstream_authusername:
    inline_string: "username"
  downstream_authpassword:
    inline_string: "password"
  )EOF";

  EXPECT_THROW(parseProtoFromYaml(yaml_string), Envoy::EnvoyException);
}

TEST_F(MySQLProxyFilterConfigTest, DownstreamAuthPasswordSet) {
  const std::string yaml_string = R"EOF(
  stat_prefix: foo
  downstream_auth_password:
    inline_string: somepassword
  )EOF";

  auto proto_config = parseProtoFromYaml(yaml_string);
  MySQLFilterConfig config(store_, proto_config, api_);
  EXPECT_EQ(config.password_, "somepassword");
  EXPECT_TRUE(config.username_.empty());
}

TEST_F(MySQLProxyFilterConfigTest, DownstreamAuthAclSet) {
  const std::string yaml_string = R"EOF(
  stat_prefix: foo
  downstream_auth_username:
    inline_string: someusername
  downstream_auth_password:
    inline_string: somepassword
  )EOF";

  auto proto_config = parseProtoFromYaml(yaml_string);
  MySQLFilterConfig config(store_, proto_config, api_);
  EXPECT_EQ(config.password_, "somepassword");
  EXPECT_EQ(config.username_, "someusername");
}

class MySQLFilterTest : public ::testing::TestWithParam<ConnPool::MySQLPoolFailureReason>,
                        public DecoderFactory,
                        public ClientFactory {
public:
  const std::string yaml_string = R"EOF(
  downstream_auth_username:
    inline_string: username
  downstream_auth_password:
    inline_string: password
  routes:
  - database: db
    cluster: cluster
  stat_prefix: foo
  )EOF";

  ClientPtr create(Tcp::ConnectionPool::ConnectionDataPtr&&, DecoderFactory&,
                   ClientCallBack&) override {
    return std::unique_ptr<MockClient>(client_);
  }

  DecoderPtr create(DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return DecoderPtr{decoder_};
  }
  MySQLFilterTest() {
    auto proto_config = parseProtoFromYaml(yaml_string);
    MySQLFilterConfigSharedPtr config =
        std::make_shared<MySQLFilterConfig>(store_, proto_config, api_);
    pool_ = std::make_unique<ConnPool::MockConnectionPoolManager>("");
    route_ = std::make_shared<MockRoute>(pool_.get());
    router_ = std::make_shared<MockRouter>(route_);
    filter_ = std::make_unique<MySQLFilter>(config, router_, *this, *this);
    EXPECT_CALL(filter_callbacks_.connection_, addConnectionCallbacks);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    EXPECT_CALL(*decoder_, getSession).Times(2);
    EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
          ServerGreeting greet{};
          BufferHelper::consumeHdr(buffer);
          greet.decode(buffer, 0, buffer.length());
          seed_ = greet.getAuthPluginData();
          auth_method_ = AuthHelper::authMethod(greet.getServerCap(), greet.getAuthPluginName());
        }));
    EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);
  }

  AuthMethod auth_method_;
  std::vector<uint8_t> seed_;
  MockClient* client_{new MockClient()};
  MySQLSession session_;
  MockDecoder* decoder_{new MockDecoder(session_)};
  DecoderCallbacks* decoder_callbacks_{};
  std::shared_ptr<MockRoute> route_;
  std::shared_ptr<MockRouter> router_;
  std::unique_ptr<ConnPool::MockConnectionPoolManager> pool_;
  Stats::TestUtil::TestStore store_;
  MySQLFilterConfigSharedPtr config_;
  std::unique_ptr<MySQLFilter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Api::MockApi> api_;
};

void etractBufferData(MySQLCodec& message, Buffer::Instance& data, uint8_t expect_seq,
                      uint32_t expect_len) {
  uint8_t seq;
  uint32_t len;
  BufferHelper::peekHdr(data, len, seq);
  EXPECT_EQ(seq, expect_seq);
  EXPECT_EQ(len, expect_len);
  BufferHelper::consumeHdr(data);
  message.decode(data, seq, len);
}

TEST_F(MySQLFilterTest, WrongUsername) {
  std::string username = "wrong_username";
  std::string db = "db";
  std::string password = "password";
  auto client_login = MessageHelper::encodeClientLogin(auth_method_, username, password, db, seed_);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(filter_callbacks_.connection_, addressProvider);
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        ErrMessage err{};
        etractBufferData(err, data, 2, data.length() - 4);
        EXPECT_EQ(err.getErrorCode(), ER_ACCESS_DENIED_ERROR);
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, WrongDb) {
  std::string username = "username";
  std::string db = "wrong_db";
  std::string password = "password";
  auto client_login = MessageHelper::encodeClientLogin(auth_method_, username, password, db, seed_);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*router_, upstreamPool(db)).WillOnce(Invoke([&](const std::string&) {
    return nullptr;
  }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        ErrMessage err{};
        etractBufferData(err, data, 2, data.length() - 4);
        EXPECT_EQ(err.getErrorCode(), ER_ER_BAD_DB_ERROR);
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, SslUpgrade) {
  std::string username = "username";
  std::string db = "wrong_db";
  std::string password = "password";
  auto client_login = MessageHelper::encodeSslUpgrade();
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, WrongOldPasswordLength) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::OldPassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 8);
  client_login.setAuthResp(MySQLTestUtils::getAuthPluginData20());
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        ErrMessage err{};
        etractBufferData(err, data, 2, data.length() - 4);
        EXPECT_EQ(err.getErrorCode(), ER_PASSWD_LENGTH);
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, WrongNativePasswordLength) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  client_login.setAuthResp(MySQLTestUtils::getAuthPluginData8());
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*router_, upstreamPool(db));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        ErrMessage err{};
        etractBufferData(err, data, 2, data.length() - 4);
        EXPECT_EQ(err.getErrorCode(), ER_PASSWD_LENGTH);
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, WrongOldPassword) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "wrong_password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::OldPassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 8);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*router_, upstreamPool(db));
  ;
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        ErrMessage err{};
        etractBufferData(err, data, 2, data.length() - 4);
        EXPECT_EQ(err.getErrorCode(), ER_ACCESS_DENIED_ERROR);
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, WrongNativePassword) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "wrong_password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*router_, upstreamPool(db));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        ErrMessage err{};
        etractBufferData(err, data, 2, data.length() - 4);
        EXPECT_EQ(err.getErrorCode(), ER_ACCESS_DENIED_ERROR);
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, OtherAuthPlugin) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::Sha256Password, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  client_login.setAuthPluginName("sha256_password");
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*router_, upstreamPool(db));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        AuthSwitchMessage auth_switch{};
        etractBufferData(auth_switch, data, 2, data.length() - 4);
        EXPECT_EQ(auth_switch.getAuthPluginName(), "mysql_native_password");
      }));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, PassAuthWriteQueryButUpstreamClientNotReady) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));
  EXPECT_CALL(*decoder_, getSession()).Times(2);
  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(*route_, upstream());

  EXPECT_CALL(*pool_, newConnection(_));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        OkMessage ok{};
        etractBufferData(ok, data, 2, data.length() - 4);
      }));

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);

  std::string query = "select * from test";
  auto cmd = MessageHelper::encodeCommand(Command::Cmd::Query, query, "", true);
  buffer = MessageHelper::encodePacket(cmd, 0);
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::StopIteration);
}

TEST_F(MySQLFilterTest, PassAuthWriteQueryAndUpstreamClientIsReady) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData)
      .WillOnce(Invoke([&](Buffer::Instance& data) {
        ClientLogin login{};
        etractBufferData(login, data, 1, data.length() - 4);
        decoder_callbacks_->onClientLogin(login);
      }))
      .WillOnce(Invoke([&](Buffer::Instance& data) {
        Command command{};
        etractBufferData(command, data, 0, data.length() - 4);
        decoder_callbacks_->onCommand(command);
      }));

  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(*route_, upstream());

  EXPECT_CALL(filter_callbacks_, continueReading());
  EXPECT_CALL(*decoder_, getSession()).Times(4);
  auto* client_data = new Tcp::ConnectionPool::MockConnectionData();

  EXPECT_CALL(*client_, makeRequest(_));
  EXPECT_CALL(*pool_, newConnection(_))
      .WillOnce(
          Invoke([&](ConnPool::ClientPoolCallBack& callbacks) -> Tcp::ConnectionPool::Cancellable* {
            callbacks.onPoolReady(
                std::unique_ptr<Tcp::ConnectionPool::MockConnectionData>(client_data), nullptr);
            return nullptr;
          }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        OkMessage ok{};
        etractBufferData(ok, data, 2, data.length() - 4);
      }))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        CommandResponse cmd_resp{};
        etractBufferData(cmd_resp, data, 1, data.length() - 4);
      }));

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);

  std::string query = "select * from test";
  auto cmd = MessageHelper::encodeCommand(Command::Cmd::Query, query, "", true);
  buffer = MessageHelper::encodePacket(cmd, 0);

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
  std::string response = "command response";
  auto cmd_resp = MessageHelper::encodeCommandResponse(response);
  filter_->onResponse(cmd_resp, 1);
}

INSTANTIATE_TEST_CASE_P(FailureTest, MySQLFilterTest,
                        ::testing::Values(ConnPool::MySQLPoolFailureReason::LocalConnectionFailure,
                                          ConnPool::MySQLPoolFailureReason::RemoteConnectionFailure,
                                          ConnPool::MySQLPoolFailureReason::Timeout,
                                          ConnPool::MySQLPoolFailureReason::Overflow,
                                          ConnPool::MySQLPoolFailureReason::AuthFailure,
                                          ConnPool::MySQLPoolFailureReason::ParseFailure,
                                          static_cast<ConnPool::MySQLPoolFailureReason>(-1)));

TEST_P(MySQLFilterTest, PassAuthWriteQueryButPoolFailure) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));

  EXPECT_CALL(*decoder_, getSession()).Times(2);
  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(*route_, upstream());

  EXPECT_CALL(*pool_, newConnection(_))
      .WillOnce(
          Invoke([&](ConnPool::ClientPoolCallBack& callbacks) -> Tcp::ConnectionPool::Cancellable* {
            callbacks.onPoolFailure(GetParam(), nullptr);
            return nullptr;
          }));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        OkMessage ok{};
        etractBufferData(ok, data, 2, data.length() - 4);
      }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);

  std::string query = "select * from test";
  auto cmd = MessageHelper::encodeCommand(Command::Cmd::Query, query, "", true);
  buffer = MessageHelper::encodePacket(cmd, 0);

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::StopIteration);
}

TEST_F(MySQLFilterTest, PassAuthAndQueryParseError) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData)
      .WillOnce(Invoke([&](Buffer::Instance& data) {
        ClientLogin login{};
        etractBufferData(login, data, 1, data.length() - 4);
        decoder_callbacks_->onClientLogin(login);
      }))
      .WillOnce(Invoke([&](Buffer::Instance& data) {
        Command command{};
        etractBufferData(command, data, 0, data.length() - 4);
        decoder_callbacks_->onCommand(command);
      }));

  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(*route_, upstream());

  EXPECT_CALL(filter_callbacks_, continueReading());
  EXPECT_CALL(*decoder_, getSession()).Times(4);

  auto* client_data = new Tcp::ConnectionPool::MockConnectionData();

  EXPECT_CALL(*client_, makeRequest(_));
  EXPECT_CALL(*pool_, newConnection(_))
      .WillOnce(
          Invoke([&](ConnPool::ClientPoolCallBack& callbacks) -> Tcp::ConnectionPool::Cancellable* {
            callbacks.onPoolReady(
                std::unique_ptr<Tcp::ConnectionPool::MockConnectionData>(client_data), nullptr);
            return nullptr;
          }));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        OkMessage ok{};
        etractBufferData(ok, data, 2, data.length() - 4);
      }));

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);

  std::string query = "select * frm test";
  auto cmd = MessageHelper::encodeCommand(Command::Cmd::Query, query, "", true);
  buffer = MessageHelper::encodePacket(cmd, 0);

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, PassAuthAndQuitCommand) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData)
      .WillOnce(Invoke([&](Buffer::Instance& data) {
        ClientLogin login{};
        etractBufferData(login, data, 1, data.length() - 4);
        decoder_callbacks_->onClientLogin(login);
      }))
      .WillOnce(Invoke([&](Buffer::Instance& data) {
        Command command{};
        etractBufferData(command, data, 0, data.length() - 4);
        decoder_callbacks_->onCommand(command);
      }));

  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(*route_, upstream());

  EXPECT_CALL(filter_callbacks_, continueReading());
  EXPECT_CALL(*decoder_, getSession()).Times(2);
  auto* client_data = new Tcp::ConnectionPool::MockConnectionData();

  EXPECT_CALL(*pool_, newConnection(_))
      .WillOnce(
          Invoke([&](ConnPool::ClientPoolCallBack& callbacks) -> Tcp::ConnectionPool::Cancellable* {
            callbacks.onPoolReady(
                std::unique_ptr<Tcp::ConnectionPool::MockConnectionData>(client_data), nullptr);
            return nullptr;
          }));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        OkMessage ok{};
        etractBufferData(ok, data, 2, data.length() - 4);
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush))
      .WillOnce(Invoke([&](Network::ConnectionCloseType) {
        filter_->onEvent(Network::ConnectionEvent::RemoteClose);
      }));
  EXPECT_CALL(*client_, close);
  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);

  auto cmd = MessageHelper::encodeCommand(Command::Cmd::Quit, "", "", false);
  buffer = MessageHelper::encodePacket(cmd, 0);

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(MySQLFilterTest, PassAuthClientNotReadyAndLocalClosed) {
  std::string username = "username";
  std::string db = "db";
  std::string password = "password";
  auto client_login =
      MessageHelper::encodeClientLogin(AuthMethod::NativePassword, username, password, db, seed_);
  EXPECT_EQ(client_login.getAuthResp().size(), 20);
  auto buffer = MessageHelper::encodePacket(client_login, 1);

  EXPECT_CALL(*decoder_, onData).WillOnce(Invoke([&](Buffer::Instance& data) {
    ClientLogin login{};
    etractBufferData(login, data, 1, data.length() - 4);
    decoder_callbacks_->onClientLogin(login);
  }));

  EXPECT_CALL(*router_, upstreamPool(db));
  EXPECT_CALL(*route_, upstream());

  EXPECT_CALL(*decoder_, getSession()).Times(2);
  auto canceller = std::make_unique<Envoy::ConnectionPool::MockCancellable>();

  EXPECT_CALL(*canceller, cancel(ConnectionPool::CancelPolicy::Default));
  EXPECT_CALL(*pool_, newConnection(_))
      .WillOnce(Invoke([&](ConnPool::ClientPoolCallBack&) -> Tcp::ConnectionPool::Cancellable* {
        return canceller.get();
      }));

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) {
        OkMessage ok{};
        etractBufferData(ok, data, 2, data.length() - 4);
      }));

  EXPECT_EQ(filter_->onData(buffer, false), Network::FilterStatus::Continue);

  filter_->onEvent(Network::ConnectionEvent::LocalClose);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy