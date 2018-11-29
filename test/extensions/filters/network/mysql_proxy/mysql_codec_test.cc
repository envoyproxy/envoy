#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

constexpr int MYSQL_UT_RESP_OK = 0;
constexpr int MYSQL_UT_LAST_ID = 0;
constexpr int MYSQL_UT_SERVER_OK = 0;
constexpr int MYSQL_UT_SERVER_WARNINGS = 0x0001;

class MySQLCodecTest : public MySQLTestUtils, public testing::Test {
protected:
  uint64_t offset_{0};
};

TEST_F(MySQLCodecTest, MySQLServerChallengeV9EncDec) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_9);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::getSalt());
  mysql_greet_encode.setSalt(salt);
  std::string data = mysql_greet_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, offset_, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), mysql_greet_encode.getSalt());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerLanguage(), 0);
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), 0);
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), 0);
  EXPECT_EQ(mysql_greet_decode.getServerCap(), 0);
}

/*
 * Test the MYSQL Greeting message V10 parser:
 * - message is encoded using the ServerGreeting class
 * - message is decoded using the ServerGreeting class
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeV10EncDec) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_10);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::getSalt());
  mysql_greet_encode.setSalt(salt);
  mysql_greet_encode.setServerCap(MYSQL_SERVER_CAPAB);
  mysql_greet_encode.setServerLanguage(MYSQL_SERVER_LANGUAGE);
  mysql_greet_encode.setServerStatus(MYSQL_SERVER_STATUS);
  mysql_greet_encode.setExtServerCap(MYSQL_SERVER_EXT_CAPAB);
  std::string data = mysql_greet_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, offset_, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), mysql_greet_encode.getSalt());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerLanguage(), mysql_greet_encode.getServerLanguage());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
}

/*
 * Test the MYSQL Client Login 41 message parser:
 * - message is encoded using the ClientLogin class
 *   - CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA set
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClLoginV41PluginAuthEncDec) {
  ClientLogin mysql_clogin_encode{};
  uint16_t client_capab = 0;
  client_capab |= (MYSQL_CLIENT_CONNECT_WITH_DB | MYSQL_CLIENT_CAPAB_41VS320);
  mysql_clogin_encode.setClientCap(client_capab);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string db = "mysql_db";
  mysql_clogin_encode.setDb(db);
  std::string data = mysql_clogin_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, offset_, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.isResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), mysql_clogin_encode.getDb());
}

/*
 * Test the MYSQL Client Login 41 message parser:
 * - message is encoded using the ClientLogin class
 *   - CLIENT_SECURE_CONNECTION set
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLogin41SecureConnEncDec) {
  ClientLogin mysql_clogin_encode{};
  uint16_t client_capab = 0;
  client_capab |= (MYSQL_CLIENT_CONNECT_WITH_DB | MYSQL_CLIENT_CAPAB_41VS320);
  mysql_clogin_encode.setClientCap(client_capab);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_SECURE_CONNECTION);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string db = "mysql_db";
  mysql_clogin_encode.setDb(db);
  std::string data = mysql_clogin_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, offset_, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.isResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), mysql_clogin_encode.getDb());
}

/*
 * Test the MYSQL Client Login 41 message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLogin41EncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(MYSQL_CLIENT_CAPAB_41VS320);
  mysql_clogin_encode.setExtendedClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, offset_, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.isResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
}

/*
 * Test the MYSQL Client Login 320 message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320EncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, offset_, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.isResponse320(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
}

/*
 * Test the MYSQL Client Login SSL message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLoginSSLEncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(MYSQL_CLIENT_CAPAB_SSL | MYSQL_CLIENT_CAPAB_41VS320);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, offset_, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.isSSLRequest(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
}

/*
 * Test the MYSQL Server Login OK message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCodecTest, MySQLLoginOkEncDec) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_UT_RESP_OK);
  mysql_loginok_encode.setAffectedRows(1);
  mysql_loginok_encode.setLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.setServerStatus(MYSQL_UT_SERVER_OK);
  mysql_loginok_encode.setWarnings(MYSQL_UT_SERVER_WARNINGS);
  std::string data = mysql_loginok_encode.encode();

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, offset_, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), mysql_loginok_encode.getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.getWarnings(), mysql_loginok_encode.getWarnings());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
