#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_query.h"

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

class MySQLCodecTest : public MySQLTestUtils, public testing::Test {};

TEST_F(MySQLCodecTest, MySQLServerChallengeV9EncDec) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.SetProtocol(MYSQL_PROTOCOL_9);
  std::string ver(MySQLTestUtils::GetVersion());
  mysql_greet_encode.SetVersion(ver);
  mysql_greet_encode.SetThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::GetSalt());
  mysql_greet_encode.SetSalt(salt);
  std::string data = mysql_greet_encode.Encode();
  std::string mysql_msg = mysql_greet_encode.EncodeHdr(data, 0);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_greet_decode.GetSalt(), mysql_greet_encode.GetSalt());
  EXPECT_EQ(mysql_greet_decode.GetVersion(), mysql_greet_encode.GetVersion());
  EXPECT_EQ(mysql_greet_decode.GetProtocol(), mysql_greet_encode.GetProtocol());
  EXPECT_EQ(mysql_greet_decode.GetThreadId(), mysql_greet_encode.GetThreadId());
  EXPECT_EQ(mysql_greet_decode.GetServerLanguage(), 0);
  EXPECT_EQ(mysql_greet_decode.GetServerStatus(), 0);
  EXPECT_EQ(mysql_greet_decode.GetExtServerCap(), 0);
  EXPECT_EQ(mysql_greet_decode.GetServerCap(), 0);
}

/*
 * Test the MYSQL Greeting message V10 parser:
 * - message is encoded using the ServerGreeting class
 * - message is decoded using the ServerGreeting class
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeV10EncDec) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.SetProtocol(MYSQL_PROTOCOL_10);
  std::string ver(MySQLTestUtils::GetVersion());
  mysql_greet_encode.SetVersion(ver);
  mysql_greet_encode.SetThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::GetSalt());
  mysql_greet_encode.SetSalt(salt);
  mysql_greet_encode.SetServerCap(MYSQL_SERVER_CAPAB);
  mysql_greet_encode.SetServerLanguage(MYSQL_SERVER_LANGUAGE);
  mysql_greet_encode.SetServerStatus(MYSQL_SERVER_STATUS);
  mysql_greet_encode.SetExtServerCap(MYSQL_SERVER_EXT_CAPAB);
  std::string data = mysql_greet_encode.Encode();
  std::string mysql_msg = mysql_greet_encode.EncodeHdr(data, 0);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_greet_decode.GetSalt(), mysql_greet_encode.GetSalt());
  EXPECT_EQ(mysql_greet_decode.GetVersion(), mysql_greet_encode.GetVersion());
  EXPECT_EQ(mysql_greet_decode.GetProtocol(), mysql_greet_encode.GetProtocol());
  EXPECT_EQ(mysql_greet_decode.GetThreadId(), mysql_greet_encode.GetThreadId());
  EXPECT_EQ(mysql_greet_decode.GetServerLanguage(), mysql_greet_encode.GetServerLanguage());
  EXPECT_EQ(mysql_greet_decode.GetServerStatus(), mysql_greet_encode.GetServerStatus());
  EXPECT_EQ(mysql_greet_decode.GetExtServerCap(), mysql_greet_encode.GetExtServerCap());
  EXPECT_EQ(mysql_greet_decode.GetServerCap(), mysql_greet_encode.GetServerCap());
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
  mysql_clogin_encode.SetClientCap(client_capab);
  mysql_clogin_encode.SetExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.SetMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.SetCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.SetUsername(user);
  std::string passwd = MySQLTestUtils::GetAuthResp();
  mysql_clogin_encode.SetAuthResp(passwd);
  std::string db = "mysql_db";
  mysql_clogin_encode.SetDB(db);
  std::string data = mysql_clogin_encode.Encode();
  std::string mysql_msg = mysql_clogin_encode.EncodeHdr(data, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_clogin_decode.IsResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.GetClientCap(), mysql_clogin_encode.GetClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetExtendedClientCap(), mysql_clogin_encode.GetExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetMaxPacket(), mysql_clogin_encode.GetMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.GetCharset(), mysql_clogin_encode.GetCharset());
  EXPECT_EQ(mysql_clogin_decode.GetUsername(), mysql_clogin_encode.GetUsername());
  EXPECT_EQ(mysql_clogin_decode.GetAuthResp(), mysql_clogin_encode.GetAuthResp());
  EXPECT_EQ(mysql_clogin_decode.GetDB(), mysql_clogin_encode.GetDB());
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
  mysql_clogin_encode.SetClientCap(client_capab);
  mysql_clogin_encode.SetExtendedClientCap(MYSQL_EXT_CL_SECURE_CONNECTION);
  mysql_clogin_encode.SetMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.SetCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.SetUsername(user);
  std::string passwd = MySQLTestUtils::GetAuthResp();
  mysql_clogin_encode.SetAuthResp(passwd);
  std::string db = "mysql_db";
  mysql_clogin_encode.SetDB(db);
  std::string data = mysql_clogin_encode.Encode();
  std::string mysql_msg = mysql_clogin_encode.EncodeHdr(data, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_clogin_decode.IsResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.GetClientCap(), mysql_clogin_encode.GetClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetExtendedClientCap(), mysql_clogin_encode.GetExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetMaxPacket(), mysql_clogin_encode.GetMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.GetCharset(), mysql_clogin_encode.GetCharset());
  EXPECT_EQ(mysql_clogin_decode.GetUsername(), mysql_clogin_encode.GetUsername());
  EXPECT_EQ(mysql_clogin_decode.GetAuthResp(), mysql_clogin_encode.GetAuthResp());
  EXPECT_EQ(mysql_clogin_decode.GetDB(), mysql_clogin_encode.GetDB());
}

/*
 * Test the MYSQL Client Login 41 message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLogin41EncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.SetClientCap(MYSQL_CLIENT_CAPAB_41VS320);
  mysql_clogin_encode.SetExtendedClientCap(0);
  mysql_clogin_encode.SetMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.SetCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.SetUsername(user);
  std::string passwd = MySQLTestUtils::GetAuthResp();
  mysql_clogin_encode.SetAuthResp(passwd);
  std::string data = mysql_clogin_encode.Encode();
  std::string mysql_msg = mysql_clogin_encode.EncodeHdr(data, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_clogin_decode.IsResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.GetClientCap(), mysql_clogin_encode.GetClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetExtendedClientCap(), mysql_clogin_encode.GetExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetMaxPacket(), mysql_clogin_encode.GetMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.GetCharset(), mysql_clogin_encode.GetCharset());
  EXPECT_EQ(mysql_clogin_decode.GetUsername(), mysql_clogin_encode.GetUsername());
  EXPECT_EQ(mysql_clogin_decode.GetAuthResp(), mysql_clogin_encode.GetAuthResp());
}

/*
 * Test the MYSQL Client Login 320 message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320EncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.SetClientCap(0);
  mysql_clogin_encode.SetExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.SetMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.SetCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.SetUsername(user);
  std::string passwd = MySQLTestUtils::GetAuthResp();
  mysql_clogin_encode.SetAuthResp(passwd);
  std::string data = mysql_clogin_encode.Encode();
  std::string mysql_msg = mysql_clogin_encode.EncodeHdr(data, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_clogin_decode.IsResponse320(), true);
  EXPECT_EQ(mysql_clogin_decode.GetClientCap(), mysql_clogin_encode.GetClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetExtendedClientCap(), mysql_clogin_encode.GetExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetMaxPacket(), mysql_clogin_encode.GetMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.GetCharset(), mysql_clogin_encode.GetCharset());
  EXPECT_EQ(mysql_clogin_decode.GetUsername(), mysql_clogin_encode.GetUsername());
  EXPECT_EQ(mysql_clogin_decode.GetAuthResp(), mysql_clogin_encode.GetAuthResp());
}

/*
 * Test the MYSQL Client Login SSL message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCodecTest, MySQLClientLoginSSLEncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.SetClientCap(MYSQL_CLIENT_CAPAB_SSL | MYSQL_CLIENT_CAPAB_41VS320);
  mysql_clogin_encode.SetExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.SetMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.SetCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.SetUsername(user);
  std::string passwd = MySQLTestUtils::GetAuthResp();
  mysql_clogin_encode.SetAuthResp(passwd);
  std::string data = mysql_clogin_encode.Encode();
  std::string mysql_msg = mysql_clogin_encode.EncodeHdr(data, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_clogin_decode.IsSSLRequest(), true);
  EXPECT_EQ(mysql_clogin_decode.GetClientCap(), mysql_clogin_encode.GetClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetExtendedClientCap(), mysql_clogin_encode.GetExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.GetMaxPacket(), mysql_clogin_encode.GetMaxPacket());
}

/*
 * Test the MYSQL Server Login OK message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCodecTest, MySQLLoginOkEncDec) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.SetRespCode(MYSQL_UT_RESP_OK);
  mysql_loginok_encode.SetAffectedRows(1);
  mysql_loginok_encode.SetLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.SetServerStatus(MYSQL_UT_SERVER_OK);
  mysql_loginok_encode.SetWarnings(MYSQL_UT_SERVER_WARNINGS);
  std::string data = mysql_loginok_encode.Encode();
  std::string mysql_msg = mysql_loginok_encode.EncodeHdr(data, 2);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.Decode(*decode_data);
  EXPECT_EQ(mysql_loginok_decode.GetRespCode(), mysql_loginok_encode.GetRespCode());
  EXPECT_EQ(mysql_loginok_decode.GetAffectedRows(), mysql_loginok_encode.GetAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.GetLastInsertId(), mysql_loginok_encode.GetLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.GetServerStatus(), mysql_loginok_encode.GetServerStatus());
  EXPECT_EQ(mysql_loginok_decode.GetWarnings(), mysql_loginok_encode.GetWarnings());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
