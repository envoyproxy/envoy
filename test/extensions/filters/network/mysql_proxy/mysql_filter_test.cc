#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_filter.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

#define SESSIONS 5

class MySQLFilterTest : public MySQLTestUtils, public testing::Test {
public:
  MySQLFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<MySQLFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<MySQLFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  MySQLFilterConfigSharedPtr config_;
  std::unique_ptr<MySQLFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test"};
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

/**
 * Test MySQL Handshake with protocol version 41
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-ok
 */
TEST_F(MySQLFilterTest, MySqlHandshake41OkTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_41, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_REQ, filter_->getSession().GetState());
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

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_41, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_ERR);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_ERROR, filter_->getSession().GetState());
}

/**
 * Test MySQL Handshake with protocol version 320
 * SM: greeting(p=10) -> challenge-req(v320) -> serv-resp-ok
 */
TEST_F(MySQLFilterTest, MySqlHandshake320OkTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(0, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_320, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_REQ, filter_->getSession().GetState());
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

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(0, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_320, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_ERR);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_ERROR, filter_->getSession().GetState());
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

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data =
      EncodeClientLogin(MYSQL_CLIENT_CAPAB_SSL | MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_SSL_PT, filter_->getSession().GetState());
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

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(0, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_320, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));

  std::string auth_switch_resp = EncodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_AUTH_SWITCH_MORE, filter_->getSession().GetState());

  std::string srv_resp_ok_data = EncodeClientLoginResp(MYSQL_RESP_OK, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_REQ, filter_->getSession().GetState());
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

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string greeting_data2 = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data2(new Buffer::OwnedImpl(greeting_data2));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data2, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string clogin_data = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(2UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_41, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_REQ, filter_->getSession().GetState());
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

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_41, filter_->getSession().GetState());

  std::string clogin_data2 = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data2(new Buffer::OwnedImpl(clogin_data2));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data2, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_41, filter_->getSession().GetState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_REQ, filter_->getSession().GetState());
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

  std::string clogin_data = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_INIT, filter_->getSession().GetState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());
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

  std::string clogin_data = EncodeClientLogin(MYSQL_CLIENT_CAPAB_41VS320, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_INIT, filter_->getSession().GetState());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());
  EXPECT_EQ(2UL, config_->stats().protocol_errors_.value());
}

/**
 * Negative sequence
 * Test MySQL Handshake with protocol version 41
 * - send greeting messages followed by login ok
 * -> expect filte to ignore serverOK, because it has not
 *    processed Challenge message
 */
TEST_F(MySQLFilterTest, MySqlHandshake41OkGreetingLoginOKTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());
  EXPECT_EQ(1UL, config_->stats_.protocol_errors_.value());
}

/**
 * Negative Testing
 * Test MySQL Handshake with protocol version 320
 * Server responds with Auth Switch wrong sequence
 * -> expect filter to ignore auth-switch message
 *    because of wrong seq.
 */
TEST_F(MySQLFilterTest, MySqlHandshake320AuthSwitchWromgSeqTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = EncodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_REQ, filter_->getSession().GetState());

  std::string clogin_data = EncodeClientLogin(0, "user1");
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_320, filter_->getSession().GetState());

  std::string auth_switch_resp = EncodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_CHALLENGE_RESP_320, filter_->getSession().GetState());

  std::string srv_resp_data = EncodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP, filter_->getSession().GetState());

  std::string srv_resp_ok_data = EncodeClientLoginResp(MYSQL_RESP_OK, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::MYSQL_AUTH_SWITCH_RESP, filter_->getSession().GetState());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
