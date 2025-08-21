#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_filter.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int SESSIONS = 5;

class MySQLFilterTest : public testing::Test, public MySQLTestUtils {
public:
  MySQLFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<MySQLFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<MySQLFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  MySQLFilterConfigSharedPtr config_;
  std::unique_ptr<MySQLFilter> filter_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::string stat_prefix_{"test."};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

// Test New Session counter increment
TEST_F(MySQLFilterTest, NewSessionStatsTest) {
  initialize();

  for (int idx = 0; idx < SESSIONS; idx++) {
    EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  }
  EXPECT_EQ(SESSIONS, config_->stats().sessions_.value());
}

// Test that the filter falls back to tcp proxy if it cant decode
TEST_F(MySQLFilterTest, MySqlFallbackToTcpProxy) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(" "));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_errors_.value());

  Buffer::InstancePtr more_data(new Buffer::OwnedImpl("scooby doo - part 2!"));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*more_data, false));
}

/**
 * Test MySQL Handshake with protocol version 41
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-ok
 */
TEST_F(MySQLFilterTest, MySqlHandshake41OkTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with partial messages.
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-ok
 */
TEST_F(MySQLFilterTest, MySqlHandshake41PartialMessagesTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);

  Buffer::InstancePtr greet_data_part_1(
      new Buffer::OwnedImpl(greeting_data.substr(0, greeting_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data_part_1, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());

  Buffer::InstancePtr greet_data_part_2(
      new Buffer::OwnedImpl(greeting_data.substr(greeting_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data_part_2, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);

  Buffer::InstancePtr client_login_data_part_1(
      new Buffer::OwnedImpl(clogin_data.substr(0, clogin_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onData(*client_login_data_part_1, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  Buffer::InstancePtr client_login_data_part_2(
      new Buffer::OwnedImpl(clogin_data.substr(clogin_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onData(*client_login_data_part_2, false));
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);

  Buffer::InstancePtr server_resp_data_part_1(
      new Buffer::OwnedImpl(srv_resp_data.substr(0, srv_resp_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onWrite(*server_resp_data_part_1, false));
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  Buffer::InstancePtr server_resp_data_part_2(
      new Buffer::OwnedImpl(srv_resp_data.substr(srv_resp_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onWrite(*server_resp_data_part_2, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Test that the filter falls back to tcp proxy if it cant decode partial messages.
 */
TEST_F(MySQLFilterTest, MySqlFallbackPartialMessagesTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);

  Buffer::InstancePtr greet_data_part_1(
      new Buffer::OwnedImpl(greeting_data.substr(0, greeting_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data_part_1, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());

  Buffer::InstancePtr corrupt_data(new Buffer::OwnedImpl(" "));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*corrupt_data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_errors_.value());

  Buffer::InstancePtr greet_data_part_2(
      new Buffer::OwnedImpl(greeting_data.substr(greeting_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data_part_2, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);

  Buffer::InstancePtr client_login_data_part_1(
      new Buffer::OwnedImpl(clogin_data.substr(0, clogin_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onData(*client_login_data_part_1, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());

  Buffer::InstancePtr client_login_data_part_2(
      new Buffer::OwnedImpl(clogin_data.substr(clogin_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onData(*client_login_data_part_2, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());
  EXPECT_EQ(0UL, config_->stats().login_attempts_.value());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);

  Buffer::InstancePtr server_resp_data_part_1(
      new Buffer::OwnedImpl(srv_resp_data.substr(0, srv_resp_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onWrite(*server_resp_data_part_1, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());

  Buffer::InstancePtr server_resp_data_part_2(
      new Buffer::OwnedImpl(srv_resp_data.substr(srv_resp_data.length() / 2)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onWrite(*server_resp_data_part_2, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 41
 * Server responds with Error
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-err
 */
TEST_F(MySQLFilterTest, MySqlHandshake41ErrTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_ERR);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(MySQLSession::State::Error, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 41
 * Server responds with Error
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-more
 */
TEST_F(MySQLFilterTest, MySqlHandshake41AuthMoreTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_MORE);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-ok
 */
TEST_F(MySQLFilterTest, MySqlHandshake320OkTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * SM: greeting(p=10) -> challenge-req(v320) -> incomplete response code
 */
TEST_F(MySQLFilterTest, MySqlHandshake320OkTestIncomplete) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeMessage(0);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * Server responds with Error
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-err
 */
TEST_F(MySQLFilterTest, MySqlHandshake320ErrTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_ERR);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(MySQLSession::State::Error, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with SSL Request
 * State-machine moves to SSL-Pass-Through
 * SM: greeting(p=10) -> challenge-req(v320) -> SSL_PT
 */
TEST_F(MySQLFilterTest, MySqlHandshakeSSLTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data =
      encodeClientLogin(CLIENT_SSL | CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());

  Buffer::OwnedImpl query_create_index("!@#$encr$#@!");
  BufferHelper::encodeHdr(query_create_index, 2);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_create_index, false));
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * Server responds with Auth Switch
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> serv-resp-ok
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_OK, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * Server responds with Auth Switch
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> serv-resp-auth-switch[error state]
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchAuthSwitchTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * Server responds with Auth Switch and error
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> serv-resp-err
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchErrTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_ERR, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::Resync, filter_->getSession().getState());

  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::Query);
  std::string query = "CREATE DATABASE mysqldb";
  mysql_cmd_encode.setData(query);
  Buffer::OwnedImpl client_query_data;

  mysql_cmd_encode.encode(client_query_data);
  BufferHelper::encodeHdr(client_query_data, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(client_query_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().queries_parsed_.value());
}

/**
 * Test MySQL Handshake with protocol version 320
 * Server responds with Auth Switch and error
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> incomplete response code
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchIncompleteRespcode) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeMessage(0, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/**
 * Resync Test failure MySQL Handshake with protocol version 320
 * Server responds with Auth Switch and error
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> serv-resp-err -> Resync fails
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchErrFailResync) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_ERR, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::Resync, filter_->getSession().getState());

  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::Query);
  std::string query = "CREATE DATABASE mysqldb";
  mysql_cmd_encode.setData(query);
  Buffer::OwnedImpl client_query_data;
  mysql_cmd_encode.encode(client_query_data);
  BufferHelper::encodeHdr(client_query_data, 5);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(client_query_data, false));
  EXPECT_EQ(MySQLSession::State::Resync, filter_->getSession().getState());
}

/**
 * Negative Testing MySQL Handshake with protocol version 320
 * Server responds with Auth Switch More
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> serv-resp-auth-switch-more
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchMoreandMore) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_MORE, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchResp, filter_->getSession().getState());
}

/**
 * Negative Testing MySQL Handshake with protocol version 320
 * Server responds with unhandled code
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-auth-switch ->
 * -> auth_switch_resp -> serv-resp-unhandled
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchMoreandUnhandled) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(0x32, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/**
 * Negative sequence
 * Test MySQL Handshake with protocol version 41
 * - send 2 back-to-back Greeting message (duplicated message)
 * -> expect filter to ignore the second.
 */
TEST_F(MySQLFilterTest, MySqlHandshake41Ok2GreetTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string greeting_data2 = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data2(new Buffer::OwnedImpl(greeting_data2));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data2, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(2UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Negative sequence
 * Test MySQL Handshake with protocol version 41
 * - send 2 back-to-back Challenge messages.
 * -> expect the filter to ignore the second
 */
TEST_F(MySQLFilterTest, MySqlHandshake41Ok2CloginTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string clogin_data2 = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data2(new Buffer::OwnedImpl(clogin_data2));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data2, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Negative sequence
 * Test MySQL Handshake with protocol version 41
 * - send out or order challenge and greeting messages.
 * -> expect the filter to ignore the challenge,
 *    since greeting was not seen
 */
TEST_F(MySQLFilterTest, MySqlHandshake41OkOOOLoginTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());
}

/**
 * Negative sequence
 * Test MySQL Handshake with protocol version 41
 * - send out or order challenge and greeting messages
 *   followed by login ok
 * -> expect the filter to ignore initial challenge as well as
 *    serverOK because out of order
 */
TEST_F(MySQLFilterTest, MySqlHandshake41OkOOOFullLoginTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::Init, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());
  EXPECT_EQ(2UL, config_->stats().protocol_errors_.value());
}

/**
 * Negative sequence
 * Test MySQL Handshake with protocol version 41
 * - send greeting messages followed by login ok
 * -> expect filter to ignore serverOK, because it has not
 *    processed Challenge message
 */
TEST_F(MySQLFilterTest, MySqlHandshake41OkGreetingLoginOKTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats_.protocol_errors_.value());
}

/**
 * Negative Testing
 * Test MySQL Handshake with protocol version 320
 * and wrong Client Login Sequence number
 */
TEST_F(MySQLFilterTest, MySqlHandshake320WrongCloginSeqTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", 2);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());
}

/**
 * Negative Testing
 * Test MySQL Handshake with protocol version 320
 * Server responds with Auth Switch wrong sequence
 * -> expect filter to ignore auth-switch message
 *    because of wrong seq.
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchWrongSeqTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchResp, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_OK, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchResp, filter_->getSession().getState());
}

/**
 * Negative Testing
 * Test MySQL Handshake with protocol version 320
 * Server responds with unexpected code
 * -> expect filter to set state to not handled
 */
TEST_F(MySQLFilterTest, MySqlHandshake320WrongServerRespCode) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(0x53, 0);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());

  Buffer::OwnedImpl client_query_data;
  BufferHelper::encodeHdr(client_query_data, 3);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(client_query_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/**
 * Negative Testing
 * Invalid Mysql Pkt Hdr
 * -> expect filter to set state to not handled
 */
TEST_F(MySQLFilterTest, MySqlWrongHdrPkt) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(0x53, 0);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());

  Buffer::OwnedImpl client_query_data("123");
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(client_query_data, false));
  EXPECT_EQ(MySQLSession::State::NotHandled, filter_->getSession().getState());
}

/*
 * Test Mysql query handler, after handshake completes
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-ok ->
 * -> Query-request -> Query-response
 * validate counters and state-machine
 */
TEST_F(MySQLFilterTest, MySqlLoginAndQueryTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());

  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::Query);
  std::string query = "CREATE DATABASE mysqldb";
  mysql_cmd_encode.setData(query);

  Buffer::OwnedImpl client_query_data;
  mysql_cmd_encode.encode(client_query_data);
  BufferHelper::encodeHdr(client_query_data, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(client_query_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().queries_parsed_.value());

  srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, 1);
  Buffer::InstancePtr request_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*request_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());

  mysql_cmd_encode.setCmd(Command::Cmd::Query);
  query = "show databases";
  mysql_cmd_encode.setData(query);

  Buffer::OwnedImpl query_show;
  mysql_cmd_encode.encode(query_show);
  BufferHelper::encodeHdr(query_show, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_show, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(2UL, config_->stats().queries_parsed_.value());

  srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, 1);
  Buffer::InstancePtr show_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*show_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());

  mysql_cmd_encode.setCmd(Command::Cmd::Query);
  query = "CREATE TABLE students (name TEXT, student_number INTEGER, city TEXT)";
  mysql_cmd_encode.setData(query);

  Buffer::OwnedImpl query_create;
  mysql_cmd_encode.encode(query_create);
  BufferHelper::encodeHdr(query_create, 0);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_create, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(3UL, config_->stats().queries_parsed_.value());

  srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, 1);
  Buffer::InstancePtr create_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*create_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());

  mysql_cmd_encode.setCmd(Command::Cmd::Query);
  query = "CREATE index index1";
  mysql_cmd_encode.setData(query);

  Buffer::OwnedImpl query_create_index;
  mysql_cmd_encode.encode(query_create_index);
  BufferHelper::encodeHdr(query_create_index, 0);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_create_index, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(3UL, config_->stats().queries_parsed_.value());

  srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, 1);
  Buffer::InstancePtr create_index_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue,
            filter_->onData(*create_index_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());

  mysql_cmd_encode.setCmd(Command::Cmd::FieldList);
  query = "";
  mysql_cmd_encode.setData(query);

  Buffer::OwnedImpl cmd_field_list;
  mysql_cmd_encode.encode(cmd_field_list);
  BufferHelper::encodeHdr(cmd_field_list, 0);

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(cmd_field_list, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(3UL, config_->stats().queries_parsed_.value());

  srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, 1);
  Buffer::InstancePtr field_list_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*field_list_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
