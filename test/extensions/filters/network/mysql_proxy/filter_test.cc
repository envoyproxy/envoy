#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/extensions/filters/network/mysql_proxy/v3/mysql_proxy.pb.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/mysql_proxy/message_helper.h"
#include "extensions/filters/network/mysql_proxy/mysql_client.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_config.h"
#include "extensions/filters/network/mysql_proxy/mysql_decoder.h"
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"
#include "extensions/filters/network/mysql_proxy/mysql_session.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"

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
  setting:
    max_connections: 10
    max_idle_connections: 5
    start_connections: 2
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

class MySQLFilterTest : public testing::Test, public DecoderFactory, public ClientFactory {
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
  setting:
    max_connections: 10
    max_idle_connections: 5
    start_connections: 2
  )EOF";

  ClientPtr create(ConnectionPool::ClientDataPtr&&, DecoderFactory&, ClientCallBack&) override {
    return std::make_unique<MockClient>();
  }
  DecoderPtr create(DecoderCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
    return DecoderPtr{decoder_};
  }
  MySQLFilterTest() {
    auto proto_config = parseProtoFromYaml(yaml_string);
    MySQLFilterConfigSharedPtr config =
        std::make_shared<MySQLFilterConfig>(store_, proto_config, api_);
    pool_ = std::make_unique<ConnectionPool::MockPool>();
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
          auth_method_ = AuthHelper::authMethod(greet.getBaseServerCap(), greet.getExtServerCap(),
                                                greet.getAuthPluginName());
        }));
    EXPECT_EQ(filter_->onNewConnection(), Network::FilterStatus::Continue);
  }

  AuthMethod auth_method_;
  std::vector<uint8_t> seed_;
  MySQLSession session_;
  MockDecoder* decoder_{new MockDecoder(session_)};
  DecoderCallbacks* decoder_callbacks_{};
  std::shared_ptr<MockRoute> route_;
  std::shared_ptr<MockRouter> router_;
  std::unique_ptr<ConnectionPool::MockPool> pool_;
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

  EXPECT_CALL(*pool_, newMySQLClient(_));
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

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy