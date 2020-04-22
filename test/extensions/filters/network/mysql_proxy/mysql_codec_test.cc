#include "extensions/filters/network/mysql_proxy/mysql_codec.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_clogin_resp.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_command.h"
#include "extensions/filters/network/mysql_proxy/mysql_codec_greeting.h"
#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

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

class MySQLCodecTest : public testing::Test {};

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
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
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
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
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
 * Negative Testing: Server Greetings Incomplete
 * - incomplete protocol
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteProtocol) {
  ServerGreeting mysql_greet_encode{};
  std::string data = "";

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), 0);
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete version
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteVersion) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_9);
  std::string data = mysql_greet_encode.encode();
  int incomplete_size = sizeof(MYSQL_PROTOCOL_9);
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getVersion(), "");
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete thread_id
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteThreadId) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_9);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  std::string data = mysql_greet_encode.encode();
  int incomplete_size = sizeof(MYSQL_PROTOCOL_9) + ver.length() + 1;
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), 0);
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete salt
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteSalt) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_9);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  std::string data = mysql_greet_encode.encode();
  int incomplete_size = sizeof(MYSQL_PROTOCOL_9) + ver.length() + 1 + sizeof(MYSQL_THREAD_ID);
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), "");
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete Server Capabilities
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteServerCap) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_10);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::getSalt());
  mysql_greet_encode.setSalt(salt);
  std::string data = mysql_greet_encode.encode();
  int incomplete_size =
      sizeof(MYSQL_PROTOCOL_9) + ver.length() + 1 + sizeof(MYSQL_THREAD_ID) + salt.length() + 1;
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), mysql_greet_encode.getSalt());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), 0);
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete Server Status
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteServerStatus) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_10);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::getSalt());
  mysql_greet_encode.setSalt(salt);
  mysql_greet_encode.setServerCap(MYSQL_SERVER_CAPAB);
  mysql_greet_encode.setServerLanguage(MYSQL_SERVER_LANGUAGE);
  std::string data = mysql_greet_encode.encode();
  int incomplete_size = sizeof(MYSQL_PROTOCOL_9) + ver.length() + 1 + sizeof(MYSQL_THREAD_ID) +
                        salt.length() + 1 + sizeof(MYSQL_SERVER_CAPAB) +
                        sizeof(MYSQL_SERVER_LANGUAGE);
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), mysql_greet_encode.getSalt());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerLanguage(), mysql_greet_encode.getServerLanguage());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), 0);
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete extended Server Capabilities
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeIncompleteExtServerCap) {
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
  std::string data = mysql_greet_encode.encode();
  int incomplete_size = sizeof(MYSQL_PROTOCOL_9) + ver.length() + 1 + sizeof(MYSQL_THREAD_ID) +
                        salt.length() + 1 + sizeof(MYSQL_SERVER_CAPAB) +
                        sizeof(MYSQL_SERVER_LANGUAGE) + sizeof(MYSQL_SERVER_STATUS);
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), mysql_greet_encode.getSalt());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerLanguage(), mysql_greet_encode.getServerLanguage());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), 0);
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities only
 */
TEST_F(MySQLCodecTest, MySQLServerChallengeP10ServerCapOnly) {
  ServerGreeting mysql_greet_encode{};
  mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_10);
  std::string ver(MySQLTestUtils::getVersion());
  mysql_greet_encode.setVersion(ver);
  mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
  std::string salt(MySQLTestUtils::getSalt());
  mysql_greet_encode.setSalt(salt);
  mysql_greet_encode.setServerCap(MYSQL_SERVER_CAPAB);
  std::string data = mysql_greet_encode.encode();
  int incomplete_size = sizeof(MYSQL_PROTOCOL_9) + ver.length() + 1 + sizeof(MYSQL_THREAD_ID) +
                        salt.length() + 1 + sizeof(MYSQL_SERVER_CAPAB);
  data = data.substr(0, incomplete_size);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(*decode_data, GREETING_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_greet_decode.getSalt(), mysql_greet_encode.getSalt());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
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
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
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
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
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
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
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
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.isResponse320(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
}

TEST_F(MySQLCodecTest, MySQLParseLengthEncodedInteger) {
  {
    // encode 2 byte value
    Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
    uint64_t input_val = 5;
    uint64_t output_val = 0;
    BufferHelper::addUint8(*buffer, LENENCODINT_2BYTES);
    BufferHelper::addUint16(*buffer, input_val);
    EXPECT_EQ(BufferHelper::readLengthEncodedInteger(*buffer, output_val), MYSQL_SUCCESS);
    EXPECT_EQ(input_val, output_val);
  }

  {
    // encode 3 byte value
    Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
    uint64_t input_val = 5;
    uint64_t output_val = 0;
    BufferHelper::addUint8(*buffer, LENENCODINT_3BYTES);
    BufferHelper::addUint16(*buffer, input_val);
    BufferHelper::addUint8(*buffer, 0);
    EXPECT_EQ(BufferHelper::readLengthEncodedInteger(*buffer, output_val), MYSQL_SUCCESS);
    EXPECT_EQ(input_val, output_val);
  }

  {
    // encode 8 byte value
    Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
    uint64_t input_val = 5;
    uint64_t output_val = 0;
    BufferHelper::addUint8(*buffer, LENENCODINT_8BYTES);
    BufferHelper::addUint32(*buffer, input_val);
    BufferHelper::addUint32(*buffer, 0);
    EXPECT_EQ(BufferHelper::readLengthEncodedInteger(*buffer, output_val), MYSQL_SUCCESS);
    EXPECT_EQ(input_val, output_val);
  }

  {
    // encode invalid length header
    Buffer::InstancePtr buffer(new Buffer::OwnedImpl());
    uint64_t input_val = 5;
    uint64_t output_val = 0;
    BufferHelper::addUint8(*buffer, 0xff);
    BufferHelper::addUint32(*buffer, input_val);
    EXPECT_EQ(BufferHelper::readLengthEncodedInteger(*buffer, output_val), MYSQL_FAILURE);
  }
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at Client Capability
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteClientCap) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  std::string data = mysql_clogin_encode.encode();
  int client_cap_len = sizeof(uint8_t);
  data = data.substr(0, client_cap_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), 0);
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at Extended Client Capability
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteExtClientCap) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t);
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), 0);
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at Max Packet
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteMaxPacket) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), 0);
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at Charset
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteCharset) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len =
      sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) + sizeof(MYSQL_MAX_PACKET);
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), 0);
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at Unset bytes
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteUnsetBytes) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET);
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at username
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteUser) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_PLG_AUTH_CL_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), "");
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at authlen
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteAuthLen) {
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
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 1;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client Login 320 message parser:
 * Incomplete header at "authpasswd"
 */
TEST_F(MySQLCodecTest, MySQLClientLogin320IncompleteAuthPasswd) {
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
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 3;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client SSL login message parser:
 * Incomplete header at authlen
 */
TEST_F(MySQLCodecTest, MySQLClientSSLLoginIncompleteAuthLen) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_SECURE_CONNECTION);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 1;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client SSL login message parser:
 * Incomplete header at username
 */
TEST_F(MySQLCodecTest, MySQLClientSSLLoginIncompleteAuthPasswd) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(MYSQL_EXT_CL_SECURE_CONNECTION);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 3;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client login message parser:
 * Incomplete auth len
 */
TEST_F(MySQLCodecTest, MySQLClientLoginIncompleteAuthPasswd) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 3;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client login message parser:
 * Incomplete auth len
 */
TEST_F(MySQLCodecTest, MySQLClientLoginIncompleteConnectDb) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(MYSQL_CLIENT_CONNECT_WITH_DB);
  mysql_clogin_encode.setExtendedClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthResp();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string data = mysql_clogin_encode.encode();
  int incomplete_len = sizeof(uint16_t) + sizeof(MYSQL_EXT_CL_PLG_AUTH_CL_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 3 + user.length() + 2;
  data = data.substr(0, incomplete_len);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
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
  mysql_clogin_decode.decode(*decode_data, CHALLENGE_SEQ_NUM, decode_data->length());
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
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), mysql_loginok_encode.getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.getWarnings(), mysql_loginok_encode.getWarnings());
}

/*
 * Test the MYSQL Server Login Old Auth Switch message parser:
 * - message is encoded using the ClientLoginResponse class
 * - message is decoded using the ClientLoginResponse class
 */
TEST_F(MySQLCodecTest, MySQLLoginOldAuthSwitch) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_RESP_AUTH_SWITCH);
  std::string data = mysql_loginok_encode.encode();
  data = data.substr(0, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK response
 */
TEST_F(MySQLCodecTest, MySQLLoginOkIncompleteRespCode) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_UT_RESP_OK);
  std::string data;

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), 0);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK affected rows
 */
TEST_F(MySQLCodecTest, MySQLLoginOkIncompleteAffectedRows) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_UT_RESP_OK);
  mysql_loginok_encode.setAffectedRows(1);
  std::string data = mysql_loginok_encode.encode();
  data = data.substr(0, 1);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK last insert id
 */
TEST_F(MySQLCodecTest, MySQLLoginOkIncompleteLastInsertId) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_UT_RESP_OK);
  mysql_loginok_encode.setAffectedRows(1);
  mysql_loginok_encode.setLastInsertId(MYSQL_UT_LAST_ID);
  std::string data = mysql_loginok_encode.encode();
  data = data.substr(0, 2);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK server status
 */
TEST_F(MySQLCodecTest, MySQLLoginOkIncompleteServerStatus) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_UT_RESP_OK);
  mysql_loginok_encode.setAffectedRows(1);
  mysql_loginok_encode.setLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.setServerStatus(MYSQL_UT_SERVER_OK);
  std::string data = mysql_loginok_encode.encode();
  data = data.substr(0, 3);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), 0);
}

/*
 * Negative Test the MYSQL Server Login OK message parser:
 * - incomplete Client Login OK warnings
 */
TEST_F(MySQLCodecTest, MySQLLoginOkIncompleteWarnings) {
  ClientLoginResponse mysql_loginok_encode{};
  mysql_loginok_encode.setRespCode(MYSQL_UT_RESP_OK);
  mysql_loginok_encode.setAffectedRows(1);
  mysql_loginok_encode.setLastInsertId(MYSQL_UT_LAST_ID);
  mysql_loginok_encode.setServerStatus(MYSQL_UT_SERVER_OK);
  mysql_loginok_encode.setWarnings(MYSQL_UT_SERVER_WARNINGS);
  std::string data = mysql_loginok_encode.encode();
  data = data.substr(0, 5);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  ClientLoginResponse mysql_loginok_decode{};
  mysql_loginok_decode.decode(*decode_data, CHALLENGE_RESP_SEQ_NUM, decode_data->length());
  EXPECT_EQ(mysql_loginok_decode.getRespCode(), mysql_loginok_encode.getRespCode());
  EXPECT_EQ(mysql_loginok_decode.getAffectedRows(), mysql_loginok_encode.getAffectedRows());
  EXPECT_EQ(mysql_loginok_decode.getLastInsertId(), mysql_loginok_encode.getLastInsertId());
  EXPECT_EQ(mysql_loginok_decode.getServerStatus(), mysql_loginok_encode.getServerStatus());
  EXPECT_EQ(mysql_loginok_decode.getWarnings(), 0);
}

TEST_F(MySQLCodecTest, MySQLCommandError) {
  Command mysql_cmd_encode{};
  std::string data = mysql_cmd_encode.encode();
  data = "";

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(data));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, 0);
  EXPECT_EQ(mysql_cmd_decode.getCmd(), Command::Cmd::Null);
}

TEST_F(MySQLCodecTest, MySQLCommandInitDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::InitDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setData(db);
  std::string data = mysql_cmd_encode.encode();

  std::string mysql_msg = BufferHelper::encodeHdr(data, 0);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST_F(MySQLCodecTest, MySQLCommandCreateDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::CreateDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setData(db);
  std::string data = mysql_cmd_encode.encode();

  std::string mysql_msg = BufferHelper::encodeHdr(data, 0);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST_F(MySQLCodecTest, MySQLCommandDropDb) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::DropDb);
  std::string db = "mysqlDB";
  mysql_cmd_encode.setData(db);
  std::string data = mysql_cmd_encode.encode();

  std::string mysql_msg = BufferHelper::encodeHdr(data, 0);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, db.length() + 1);
  EXPECT_EQ(mysql_cmd_decode.getDb(), db);
}

TEST_F(MySQLCodecTest, MySQLCommandOther) {
  Command mysql_cmd_encode{};
  mysql_cmd_encode.setCmd(Command::Cmd::FieldList);
  std::string data = mysql_cmd_encode.encode();

  std::string mysql_msg = BufferHelper::encodeHdr(data, 0);

  Buffer::InstancePtr decode_data(new Buffer::OwnedImpl(mysql_msg));
  Command mysql_cmd_decode{};
  decode_data->drain(4);
  mysql_cmd_decode.decode(*decode_data, 0, 0);
  EXPECT_EQ(mysql_cmd_decode.getCmd(), Command::Cmd::FieldList);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
