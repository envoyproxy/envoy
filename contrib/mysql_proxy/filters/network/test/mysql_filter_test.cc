#include "source/common/buffer/buffer_impl.h"
#include "source/common/crypto/utility.h"

#include "test/mocks/network/mocks.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_clogin_resp.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_filter.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"
#include "openssl/evp.h"
#include "openssl/pem.h"
#include "openssl/rsa.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int SESSIONS = 5;

class MySQLFilterTest : public testing::Test, public MySQLTestUtils {
public:
  MySQLFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  using MySQLProxyProto = envoy::extensions::filters::network::mysql_proxy::v3::MySQLProxy;

  void initialize(MySQLProxyProto::SSLMode downstream_ssl = MySQLProxyProto::DISABLE) {
    config_ = std::make_shared<MySQLFilterConfig>(stat_prefix_, scope_, downstream_ssl);
    filter_ = std::make_unique<MySQLFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_filter_callbacks_);
  }

  // Encode a server greeting for caching_sha2_password with 20-byte scramble.
  std::string encodeServerGreetingCachingSha2() {
    ServerGreeting greeting;
    greeting.setProtocol(MYSQL_PROTOCOL_10);
    greeting.setVersion(getVersion());
    greeting.setThreadId(MYSQL_THREAD_ID);
    greeting.setAuthPluginData(getAuthPluginData20());
    greeting.setServerCap(CLIENT_PLUGIN_AUTH | CLIENT_SECURE_CONNECTION);
    greeting.setServerCharset(MYSQL_SERVER_LANGUAGE);
    greeting.setServerStatus(MYSQL_SERVER_STATUS);
    greeting.setAuthPluginName("caching_sha2_password");
    Buffer::OwnedImpl buffer;
    greeting.encode(buffer);
    BufferHelper::encodeHdr(buffer, GREETING_SEQ_NUM);
    return buffer.toString();
  }

  // Encode an AuthMoreData packet with specific data bytes.
  std::string encodeAuthMoreDataPacket(const std::vector<uint8_t>& data, uint8_t seq) {
    AuthMoreMessage auth_more;
    auth_more.setAuthMoreData(data);
    Buffer::OwnedImpl buffer;
    auth_more.encode(buffer);
    BufferHelper::encodeHdr(buffer, seq);
    return buffer.toString();
  }

  // Encode a raw client-to-server packet (e.g., cleartext password).
  std::string encodeRawPacket(const std::string& payload, uint8_t seq) {
    Buffer::OwnedImpl buffer;
    BufferHelper::addString(buffer, payload);
    BufferHelper::encodeHdr(buffer, seq);
    return buffer.toString();
  }

  MySQLFilterConfigSharedPtr config_;
  std::unique_ptr<MySQLFilter> filter_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::string stat_prefix_{"test."};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_filter_callbacks_;
  NiceMock<Network::MockConnection> connection_;
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(1UL, config_->stats().decoder_errors_.value());

  Buffer::InstancePtr more_data(new Buffer::OwnedImpl("scooby doo - part 2!"));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*more_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_ERR);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(MySQLSession::State::Error, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with protocol version 41
 * Server responds with Auth More Data
 * SM: greeting(p=10) -> challenge-req(v41) -> serv-resp-more
 */
TEST_F(MySQLFilterTest, MySqlHandshake41AuthMoreTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_MORE);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeMessage(0);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_ERR);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(MySQLSession::State::Error, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with SSL Request
 * State-machine moves to SSL-Pass-Through
 * SM: greeting(p=10) -> challenge-req(v320) -> SSL_PT
 */
TEST_F(MySQLFilterTest, MySqlHandshakeSSLPassThroughTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  // Send SSL Connection Request packet.
  // https://dev.mysql.com/doc/internals/en/ssl-handshake.html
  std::string clogin_data = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());

  // After SSL handshaking, attempt to login.
  // Since the SSL-Pass-Through, # of login attempts is unknown.
  clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  client_login_data = Buffer::InstancePtr(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, CHALLENGE_RESP_SEQ_NUM + 1);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());

  Buffer::OwnedImpl query_create_index("!@#$encr$#@!");
  BufferHelper::encodeHdr(query_create_index, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_create_index, false));
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());
}

/**
 * Test MySQL Handshake with SSL Request
 * State-machine moves to SSL-Terminate
 * SM: greeting(p=10) -> challenge-req(v320) -> SSL_PT -> ChallengeReq -> Req -> ReqResp
 */
TEST_F(MySQLFilterTest, MySqlHandshakeSSLTerminateTest) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1UL, config_->stats().sessions_.value());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));

  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_CALL(connection_, close(_)).Times(0);

  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration,
            filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  client_login_data = Buffer::InstancePtr(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());

  Buffer::OwnedImpl query_create_index("!@#$encr$#@!");
  BufferHelper::encodeHdr(query_create_index, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_create_index, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_OK, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_ERR, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeMessage(0, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_ERR, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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
 * MySQL Handshake with protocol version 320
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_MORE, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_AUTH_SWITCH);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));

  std::string auth_switch_resp = encodeAuthSwitchResp();
  Buffer::InstancePtr client_switch_resp(new Buffer::OwnedImpl(auth_switch_resp));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_switch_resp, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(0x32, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string greeting_data2 = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data2(new Buffer::OwnedImpl(greeting_data2));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data2, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(1UL, config_->stats().protocol_errors_.value());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchResp, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(MYSQL_RESP_OK, 1);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(0x53, 0);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(0, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp320, filter_->getSession().getState());

  std::string srv_resp_ok_data = encodeClientLoginResp(0x53, 0);
  Buffer::InstancePtr server_resp_ok_data(new Buffer::OwnedImpl(srv_resp_ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_ok_data, false));
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

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*request_resp_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*show_resp_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*create_resp_data, false));
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
            filter_->onWrite(*create_index_resp_data, false));
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
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*field_list_resp_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
}

/**
 * Test RSA mediation for caching_sha2_password full authentication flow.
 * SSL termination + cache miss: client sends cleartext password over TLS,
 * filter RSA-encrypts it for the plaintext upstream connection.
 */
TEST_F(MySQLFilterTest, MySqlCachingSha2FullAuthRsaMediation) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Step 1: Server greeting with caching_sha2_password plugin.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  // Step 2: Client SSL request (seq=1).
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  // Step 3: Client login (seq=2 from client, should be rewritten to seq=1 for server).
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  // Step 4: Server responds with AuthMoreData(0x04) = full auth required (raw seq=2).
  std::string auth_more_data =
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(auth_more_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());
  EXPECT_EQ(RsaAuthState::WaitingClientPassword, filter_->getRsaAuthState());

  // Step 5: Client sends cleartext password (seq=4 from client perspective).
  // The filter should intercept this and inject a request-public-key packet.
  std::string password = "secret";
  std::string pw_payload = password + '\0';
  std::string pw_data = encodeRawPacket(pw_payload, 4);

  Buffer::OwnedImpl captured_request_key;
  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillOnce([&](Buffer::Instance& buf, bool) { captured_request_key.add(buf); });

  Buffer::InstancePtr pw_buf(new Buffer::OwnedImpl(pw_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*pw_buf, false));
  EXPECT_EQ(RsaAuthState::WaitingServerKey, filter_->getRsaAuthState());

  // Verify the request-public-key packet: [len=1][seq=3][0x02]
  ASSERT_EQ(5u, captured_request_key.length());
  uint32_t req_len = 0;
  uint8_t req_seq = 0;
  BufferHelper::peekHdr(captured_request_key, req_len, req_seq);
  EXPECT_EQ(1u, req_len);
  EXPECT_EQ(3u, req_seq);
  BufferHelper::consumeHdr(captured_request_key);
  uint8_t req_code;
  BufferHelper::readUint8(captured_request_key, req_code);
  EXPECT_EQ(MYSQL_REQUEST_PUBLIC_KEY, req_code);

  // Step 6: Generate an RSA-2048 key pair for the test.
  bssl::UniquePtr<EVP_PKEY_CTX> gen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  ASSERT_TRUE(gen_ctx);
  ASSERT_GT(EVP_PKEY_keygen_init(gen_ctx.get()), 0);
  ASSERT_GT(EVP_PKEY_CTX_set_rsa_keygen_bits(gen_ctx.get(), 2048), 0);
  EVP_PKEY* raw_pkey = nullptr;
  ASSERT_GT(EVP_PKEY_keygen(gen_ctx.get(), &raw_pkey), 0);
  bssl::UniquePtr<EVP_PKEY> pkey(raw_pkey);

  // Extract public key PEM.
  bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
  PEM_write_bio_PUBKEY(bio.get(), pkey.get());
  char* pem_data;
  long pem_len = BIO_get_mem_data(bio.get(), &pem_data);
  std::string pem_key(pem_data, pem_len);

  // Step 7: Server sends PEM key as AuthMoreData (raw seq=4).
  // The filter should intercept this and inject the RSA-encrypted password.
  Buffer::OwnedImpl captured_encrypted_pw;
  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillOnce([&](Buffer::Instance& buf, bool) { captured_encrypted_pw.add(buf); });

  std::string key_packet =
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4);
  Buffer::InstancePtr key_buf(new Buffer::OwnedImpl(key_packet));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*key_buf, false));
  EXPECT_EQ(RsaAuthState::WaitingServerResult, filter_->getRsaAuthState());

  // Verify the encrypted password packet header: [len=256][seq=5][encrypted_data]
  ASSERT_GT(captured_encrypted_pw.length(), 4u);
  uint32_t enc_len = 0;
  uint8_t enc_seq = 0;
  BufferHelper::peekHdr(captured_encrypted_pw, enc_len, enc_seq);
  EXPECT_EQ(256u, enc_len); // RSA-2048 produces 256-byte ciphertext
  EXPECT_EQ(5u, enc_seq);

  // Decrypt and verify the password XOR scramble.
  BufferHelper::consumeHdr(captured_encrypted_pw);
  std::string ciphertext;
  BufferHelper::readStringBySize(captured_encrypted_pw, enc_len, ciphertext);

  bssl::UniquePtr<EVP_PKEY_CTX> dec_ctx(EVP_PKEY_CTX_new(pkey.get(), nullptr));
  ASSERT_TRUE(dec_ctx);
  ASSERT_GT(EVP_PKEY_decrypt_init(dec_ctx.get()), 0);
  ASSERT_GT(EVP_PKEY_CTX_set_rsa_padding(dec_ctx.get(), RSA_PKCS1_OAEP_PADDING), 0);
  ASSERT_GT(EVP_PKEY_CTX_set_rsa_oaep_md(dec_ctx.get(), EVP_sha1()), 0);

  size_t plain_len = 0;
  ASSERT_GT(EVP_PKEY_decrypt(dec_ctx.get(), nullptr, &plain_len,
                             reinterpret_cast<const uint8_t*>(ciphertext.data()),
                             ciphertext.size()),
            0);
  std::vector<uint8_t> plaintext(plain_len);
  ASSERT_GT(EVP_PKEY_decrypt(dec_ctx.get(), plaintext.data(), &plain_len,
                             reinterpret_cast<const uint8_t*>(ciphertext.data()),
                             ciphertext.size()),
            0);
  plaintext.resize(plain_len);

  // plaintext = (password + \0) XOR scramble (cyclic 20-byte)
  std::vector<uint8_t> scramble = getAuthPluginData20(); // 20 bytes of 0xff
  ASSERT_EQ(pw_payload.size(), plaintext.size());
  for (size_t i = 0; i < plaintext.size(); i++) {
    uint8_t expected = static_cast<uint8_t>(pw_payload[i]) ^ scramble[i % scramble.size()];
    EXPECT_EQ(expected, plaintext[i]) << "mismatch at byte " << i;
  }

  // Step 8: Server sends OK (raw seq=6, rewritten to seq=5 for client).
  std::string srv_ok = encodeClientLoginResp(MYSQL_RESP_OK, 0, 6);
  Buffer::InstancePtr ok_buf(new Buffer::OwnedImpl(srv_ok));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*ok_buf, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * Test that fast auth success (AuthMoreData 0x03) passes through without RSA mediation.
 */
TEST_F(MySQLFilterTest, MySqlCachingSha2FastAuthPassthrough) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting with caching_sha2_password.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // SSL request.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));

  // Client login.
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  // Server responds with AuthMoreData(0x03) = fast auth success.
  std::string auth_more_data =
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FAST_AUTH_SUCCESS}, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(auth_more_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());
  // RSA mediation should NOT be triggered.
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());

  // Server sends OK (raw seq=3: next seq after server's AuthMoreData at seq=2).
  std::string srv_ok = encodeClientLoginResp(MYSQL_RESP_OK, 0, 3);
  Buffer::InstancePtr ok_buf(new Buffer::OwnedImpl(srv_ok));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*ok_buf, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * Test RSA mediation when server returns ERR after encrypted password.
 */
TEST_F(MySQLFilterTest, MySqlCachingSha2FullAuthRsaErr) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // SSL request.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));

  // Client login.
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));

  // AuthMoreData(0x04) = full auth required.
  std::string auth_more_data =
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(auth_more_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(RsaAuthState::WaitingClientPassword, filter_->getRsaAuthState());

  // Client password.
  std::string pw_data = encodeRawPacket(std::string("secret") + '\0', 4);

  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false));
  Buffer::InstancePtr pw_buf(new Buffer::OwnedImpl(pw_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*pw_buf, false));
  EXPECT_EQ(RsaAuthState::WaitingServerKey, filter_->getRsaAuthState());

  // Generate RSA key pair and send PEM key.
  bssl::UniquePtr<EVP_PKEY_CTX> gen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  ASSERT_TRUE(gen_ctx);
  ASSERT_GT(EVP_PKEY_keygen_init(gen_ctx.get()), 0);
  ASSERT_GT(EVP_PKEY_CTX_set_rsa_keygen_bits(gen_ctx.get(), 2048), 0);
  EVP_PKEY* raw_pkey = nullptr;
  ASSERT_GT(EVP_PKEY_keygen(gen_ctx.get(), &raw_pkey), 0);
  bssl::UniquePtr<EVP_PKEY> pkey(raw_pkey);
  bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
  PEM_write_bio_PUBKEY(bio.get(), pkey.get());
  char* pem_data;
  long pem_len = BIO_get_mem_data(bio.get(), &pem_data);
  std::string pem_key(pem_data, pem_len);

  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false));
  std::string key_packet =
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4);
  Buffer::InstancePtr key_buf(new Buffer::OwnedImpl(key_packet));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*key_buf, false));
  EXPECT_EQ(RsaAuthState::WaitingServerResult, filter_->getRsaAuthState());

  // Server responds with ERR (raw seq=6).
  std::string srv_err = encodeClientLoginResp(MYSQL_RESP_ERR, 0, 6);
  Buffer::InstancePtr err_buf(new Buffer::OwnedImpl(srv_err));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*err_buf, false));
  EXPECT_EQ(1UL, config_->stats().login_failures_.value());
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * Test that RSA mediation is NOT triggered when downstream_ssl is DISABLE.
 */
TEST_F(MySQLFilterTest, MySqlCachingSha2NoTerminateSsl) {
  initialize(); // downstream_ssl = DISABLE

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting with caching_sha2_password.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeReq, filter_->getSession().getState());

  // Client login (no SSL in this path).
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(MySQLSession::State::ChallengeResp41, filter_->getSession().getState());

  // AuthMoreData(0x04) from server.
  std::string auth_more_data =
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(auth_more_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  // RSA mediation should NOT be triggered because downstream_ssl is DISABLE.
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

// =============================================================================
// Extended coverage: SSL Terminated, SSL Passthrough, No-SSL with queries
// =============================================================================

/**
 * SSL Terminated: basic login with mysql_native_password (no caching_sha2).
 * Verifies seq rewriting without RSA mediation.
 */
TEST_F(MySQLFilterTest, MySqlSslTerminateNativePasswordLoginOk) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting (standard, no caching_sha2_password).
  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // SSL request.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());

  // Client login (seq=2, rewritten to seq=1).
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());

  // Server OK (seq=2, rewritten to seq=3).
  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * SSL Terminated: login followed by query execution.
 * Verifies the filter works correctly after auth completes.
 */
TEST_F(MySQLFilterTest, MySqlSslTerminateLoginThenQuery) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Greeting + SSL + Login + OK (same as native password test above).
  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());

  // Now send a query — should be processed normally after auth.
  Command mysql_cmd{};
  mysql_cmd.setCmd(Command::Cmd::Query);
  mysql_cmd.setData("SELECT 1");
  Buffer::OwnedImpl query_data;
  mysql_cmd.encode(query_data);
  BufferHelper::encodeHdr(query_data, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().queries_parsed_.value());
}

/**
 * SSL Terminated: full auth RSA followed by query execution.
 * End-to-end: greeting → SSL → login → AuthMore(0x04) → pw → RSA → OK → query.
 */
TEST_F(MySQLFilterTest, MySqlSslTerminateRsaThenQuery) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Greeting with caching_sha2_password.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // SSL + login.
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  Buffer::InstancePtr ssl_data(
      new Buffer::OwnedImpl(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));

  Buffer::InstancePtr login_data(
      new Buffer::OwnedImpl(encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*login_data, false));

  // AuthMoreData(0x04).
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(RsaAuthState::WaitingClientPassword, filter_->getRsaAuthState());

  // Client password → intercepted, request-public-key injected.
  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false));
  Buffer::InstancePtr pw_buf(
      new Buffer::OwnedImpl(encodeRawPacket(std::string("secret") + '\0', 4)));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*pw_buf, false));

  // PEM key → intercepted, encrypted password injected.
  bssl::UniquePtr<EVP_PKEY_CTX> gen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
  EVP_PKEY_keygen_init(gen_ctx.get());
  EVP_PKEY_CTX_set_rsa_keygen_bits(gen_ctx.get(), 2048);
  EVP_PKEY* raw_pkey = nullptr;
  EVP_PKEY_keygen(gen_ctx.get(), &raw_pkey);
  bssl::UniquePtr<EVP_PKEY> pkey(raw_pkey);
  bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
  PEM_write_bio_PUBKEY(bio.get(), pkey.get());
  char* pem_data;
  long pem_len = BIO_get_mem_data(bio.get(), &pem_data);
  std::string pem_key(pem_data, pem_len);

  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false));
  Buffer::InstancePtr key_buf(new Buffer::OwnedImpl(
      encodeAuthMoreDataPacket(std::vector<uint8_t>(pem_key.begin(), pem_key.end()), 4)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*key_buf, false));
  EXPECT_EQ(RsaAuthState::WaitingServerResult, filter_->getRsaAuthState());

  // Server OK (raw seq=6).
  Buffer::InstancePtr ok_buf(new Buffer::OwnedImpl(encodeClientLoginResp(MYSQL_RESP_OK, 0, 6)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*ok_buf, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());

  // Query after RSA auth — verify seq numbers are correct and filter works.
  Command mysql_cmd{};
  mysql_cmd.setCmd(Command::Cmd::Query);
  mysql_cmd.setData("SELECT 1");
  Buffer::OwnedImpl query_data;
  mysql_cmd.encode(query_data);
  BufferHelper::encodeHdr(query_data, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().queries_parsed_.value());
}

/**
 * SSL Passthrough: server greeting with SSL → SSL pass-through → auth more data.
 * Verifies that caching_sha2 auth more goes through unmodified in passthrough mode.
 */
TEST_F(MySQLFilterTest, MySqlSslPassthroughCachingSha2) {
  initialize(); // downstream_ssl = DISABLE

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting (passes through).
  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client SSL request → enters SslPt (passthrough) state.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*ssl_data, false));
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());

  // All further data is opaque (encrypted), filter just passes through.
  Buffer::OwnedImpl encrypted_data("encrypted-login-packet-bytes");
  BufferHelper::encodeHdr(encrypted_data, CHALLENGE_SEQ_NUM + 1);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(encrypted_data, false));
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());

  // RSA mediation never triggers.
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * SSL Terminated: startSecureTransport() fails → connection closed.
 */
TEST_F(MySQLFilterTest, MySqlSslTerminateStartTlsFails) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // SSL request, but startSecureTransport fails.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(false));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));
}

/**
 * No-SSL: caching_sha2 fast auth (0x03) without any SSL involved.
 * Verifies the filter handles auth more data correctly without SSL termination.
 */
TEST_F(MySQLFilterTest, MySqlNoSslCachingSha2FastAuth) {
  initialize(); // downstream_ssl = DISABLE

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting with caching_sha2_password.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client login (no SSL).
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());

  // Server responds with fast auth success (0x03).
  std::string auth_more_data =
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FAST_AUTH_SUCCESS}, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(auth_more_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(MySQLSession::State::AuthSwitchMore, filter_->getSession().getState());
  // No RSA mediation without SSL termination.
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());

  // Server OK.
  std::string ok_data = encodeClientLoginResp(MYSQL_RESP_OK, 0, 3);
  Buffer::InstancePtr ok_buf(new Buffer::OwnedImpl(ok_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*ok_buf, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());
}

/**
 * No-SSL: caching_sha2 full auth (0x04) without SSL termination.
 * The filter should NOT intercept — full auth is handled by client/server directly.
 */
TEST_F(MySQLFilterTest, MySqlNoSslCachingSha2FullAuth) {
  initialize(); // downstream_ssl = DISABLE

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting with caching_sha2_password.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client login (no SSL).
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));

  // Server responds with full auth required (0x04).
  std::string auth_more_data =
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM);
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(auth_more_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  // RSA mediation NOT triggered (downstream_ssl is DISABLE).
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());

  // Client sends request-public-key (0x02) — passes through to server.
  std::string req_key = encodeRawPacket(std::string(1, MYSQL_REQUEST_PUBLIC_KEY), 3);
  Buffer::InstancePtr req_key_buf(new Buffer::OwnedImpl(req_key));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*req_key_buf, false));

  // No interception — filter stays inactive.
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * No-SSL: plain login followed by query.
 * Basic end-to-end without any SSL.
 */
TEST_F(MySQLFilterTest, MySqlNoSslLoginThenQuery) {
  initialize(); // downstream_ssl = DISABLE

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());

  std::string srv_resp_data = encodeClientLoginResp(MYSQL_RESP_OK);
  Buffer::InstancePtr server_resp_data(new Buffer::OwnedImpl(srv_resp_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*server_resp_data, false));
  EXPECT_EQ(MySQLSession::State::Req, filter_->getSession().getState());

  // Query.
  Command mysql_cmd{};
  mysql_cmd.setCmd(Command::Cmd::Query);
  mysql_cmd.setData("CREATE TABLE t (id INT)");
  Buffer::OwnedImpl query_data;
  mysql_cmd.encode(query_data);
  BufferHelper::encodeHdr(query_data, 0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(query_data, false));
  EXPECT_EQ(MySQLSession::State::ReqResp, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().queries_parsed_.value());
}

/**
 * SSL REQUIRE mode: client that does NOT send SSL request gets rejected.
 */
TEST_F(MySQLFilterTest, MySqlSslRequireRejectsNonSslClient) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Server greeting.
  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client sends login directly (no SSL request) — should be rejected.
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
}

/**
 * SSL REQUIRE mode: client that sends SSL request is accepted.
 */
TEST_F(MySQLFilterTest, MySqlSslRequireAcceptsSslClient) {
  initialize(MySQLProxyProto::REQUIRE);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client sends SSL request — accepted.
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));

  // Client login after SSL — no rejection.
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
}

/**
 * SSL ALLOW mode: client without SSL is accepted.
 */
TEST_F(MySQLFilterTest, MySqlSslAllowAcceptsNonSslClient) {
  initialize(MySQLProxyProto::ALLOW);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client sends login directly (no SSL) — should be accepted in ALLOW mode.
  std::string clogin_data = encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr client_login_data(new Buffer::OwnedImpl(clogin_data));
  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*client_login_data, false));
  EXPECT_EQ(1UL, config_->stats().login_attempts_.value());
}

/**
 * SSL ALLOW mode: client with SSL initiates TLS, RSA mediation works.
 */
TEST_F(MySQLFilterTest, MySqlSslAllowWithSslClientRsaMediation) {
  initialize(MySQLProxyProto::ALLOW);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  // Greeting with caching_sha2_password.
  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // SSL request — accepted in ALLOW mode.
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  Buffer::InstancePtr ssl_data(
      new Buffer::OwnedImpl(encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*ssl_data, false));
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());

  // Client login.
  Buffer::InstancePtr login_data(
      new Buffer::OwnedImpl(encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM + 1)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*login_data, false));

  // AuthMoreData(0x04) — full auth required, should trigger RSA mediation.
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(RsaAuthState::WaitingClientPassword, filter_->getRsaAuthState());

  // Client password — intercepted, request-public-key injected.
  EXPECT_CALL(filter_callbacks_, injectReadDataToFilterChain(_, false));
  Buffer::InstancePtr pw_buf(
      new Buffer::OwnedImpl(encodeRawPacket(std::string("secret") + '\0', 4)));
  EXPECT_EQ(Envoy::Network::FilterStatus::StopIteration, filter_->onData(*pw_buf, false));
  EXPECT_EQ(RsaAuthState::WaitingServerKey, filter_->getRsaAuthState());
}

/**
 * SSL ALLOW mode: non-SSL client with caching_sha2 — NO RSA mediation.
 * Client handles RSA directly with the server.
 */
TEST_F(MySQLFilterTest, MySqlSslAllowNonSslCachingSha2NoMediation) {
  initialize(MySQLProxyProto::ALLOW);

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client sends login directly (no SSL).
  Buffer::InstancePtr login_data(
      new Buffer::OwnedImpl(encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM)));
  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*login_data, false));

  // AuthMoreData(0x04) — should NOT trigger RSA mediation (no SSL was terminated).
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * SSL DISABLE mode: caching_sha2 full auth passes through without mediation.
 */
TEST_F(MySQLFilterTest, MySqlSslDisableCachingSha2FullAuthNoMediation) {
  initialize(); // DISABLE

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreetingCachingSha2();
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  Buffer::InstancePtr login_data(
      new Buffer::OwnedImpl(encodeClientLogin(CLIENT_PROTOCOL_41, "user1", CHALLENGE_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*login_data, false));

  // AuthMoreData(0x04) — no RSA mediation in DISABLE mode.
  Buffer::InstancePtr auth_more(new Buffer::OwnedImpl(
      encodeAuthMoreDataPacket({MYSQL_CACHINGSHA2_FULL_AUTH_REQUIRED}, CHALLENGE_RESP_SEQ_NUM)));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*auth_more, false));
  EXPECT_EQ(RsaAuthState::Inactive, filter_->getRsaAuthState());
}

/**
 * SSL DISABLE mode: SSL request passes through to server (passthrough behavior).
 */
TEST_F(MySQLFilterTest, MySqlSslDisableSslPassthrough) {
  initialize(); // DISABLE

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());

  std::string greeting_data = encodeServerGreeting(MYSQL_PROTOCOL_10);
  Buffer::InstancePtr greet_data(new Buffer::OwnedImpl(greeting_data));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onWrite(*greet_data, false));

  // Client sends SSL request — in DISABLE mode, it passes through (SslPt state).
  std::string ssl_req = encodeClientLogin(CLIENT_SSL, "", CHALLENGE_SEQ_NUM);
  Buffer::InstancePtr ssl_data(new Buffer::OwnedImpl(ssl_req));
  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onData(*ssl_data, false));
  EXPECT_EQ(MySQLSession::State::SslPt, filter_->getSession().getState());
  EXPECT_EQ(1UL, config_->stats().upgraded_to_ssl_.value());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
