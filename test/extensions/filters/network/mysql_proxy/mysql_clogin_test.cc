#include <bits/stdint-uintn.h>

#include "common/buffer/buffer_impl.h"

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

class MySQLCLoginTest : public testing::Test {};

/*
 * Test the MYSQL Client Login 41 message parser:
 * - message is encoded using the ClientLogin class
 *   - CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA set
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCLoginTest, MySQLClLoginV41PluginAuthEncDec) {
  ClientLogin mysql_clogin_encode{};
  uint32_t client_capab = 0;
  client_capab |= (CLIENT_CONNECT_WITH_DB | CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH);
  mysql_clogin_encode.setClientCap(client_capab);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  std::string db = "mysql_db";
  mysql_clogin_encode.setDb(db);
  mysql_clogin_encode.setAuthPluginName(MySQLTestUtils::getAuthPluginName());

  Buffer::OwnedImpl decode_data;
  mysql_clogin_encode.encode(decode_data);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.isResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getBaseClientCap(), mysql_clogin_encode.getBaseClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), mysql_clogin_encode.getDb());
  EXPECT_EQ(mysql_clogin_decode.getAuthPluginName(), mysql_clogin_encode.getAuthPluginName());
}

/*
 * Test the MYSQL Client Login 41 message parser:
 * - message is encoded using the ClientLogin class
 *   - CLIENT_SECURE_CONNECTION set
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41SecureConnEncDec) {
  ClientLogin mysql_clogin_encode{};
  uint32_t client_capab = 0;
  client_capab |= (CLIENT_CONNECT_WITH_DB | CLIENT_PROTOCOL_41 | CLIENT_SECURE_CONNECTION);
  mysql_clogin_encode.setClientCap(client_capab);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  std::string db = "mysql_db";
  mysql_clogin_encode.setDb(db);
  mysql_clogin_encode.setAuthPluginName(MySQLTestUtils::getAuthPluginName());
  Buffer::OwnedImpl decode_data;
  mysql_clogin_encode.encode(decode_data);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.isResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getBaseClientCap(), mysql_clogin_encode.getBaseClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), mysql_clogin_encode.getDb());
  EXPECT_EQ(mysql_clogin_decode.getAuthPluginName(), "");
}

/*
 * Test the MYSQL Client Login 41 message parser without CLIENT_CONNECT_WITH_DB:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41EncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  mysql_clogin_encode.setUsername("user");
  mysql_clogin_encode.setDb("mysql.db");
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  mysql_clogin_encode.setAuthPluginName(MySQLTestUtils::getAuthPluginName());

  Buffer::OwnedImpl decode_data;
  mysql_clogin_encode.encode(decode_data);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.isResponse41(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getBaseClientCap(), mysql_clogin_encode.getBaseClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());

  EXPECT_TRUE(mysql_clogin_decode.getAuthPluginName().empty());
  EXPECT_TRUE(mysql_clogin_decode.getDb().empty());
}

/*
 * Test the MYSQL Client Login 320 message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin320EncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  mysql_clogin_encode.setUsername("user");
  mysql_clogin_encode.setDb("mysql.db");
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  mysql_clogin_encode.setAuthPluginName(MySQLTestUtils::getAuthPluginName());

  Buffer::OwnedImpl decode_data;
  mysql_clogin_encode.encode(decode_data);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.isResponse320(), true);
  EXPECT_EQ(mysql_clogin_decode.getBaseClientCap(), mysql_clogin_encode.getBaseClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());

  EXPECT_EQ(mysql_clogin_decode.getAuthPluginName(), "");
  EXPECT_EQ(mysql_clogin_decode.getDb(), "");
  EXPECT_EQ(mysql_clogin_decode.getCharset(), 0);
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), 0);
}

TEST_F(MySQLCLoginTest, MySQLParseLengthEncodedInteger) {
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
  {
    // encode and decode length encoded integer
    uint64_t input_vals[4] = {
        5,
        251 + 5,
        (1 << 16) + 5,
        (1 << 24) + 5,
    };
    for (uint64_t& input_val : input_vals) {
      Buffer::OwnedImpl buffer;
      uint64_t output_val = 0;
      BufferHelper::addLengthEncodedInteger(buffer, input_val);
      BufferHelper::readLengthEncodedInteger(buffer, output_val);
      EXPECT_EQ(input_val, output_val);
    }
  }
  {
    // encode decode uint24
    Buffer::OwnedImpl buffer;
    uint32_t val = 0xfffefd;
    BufferHelper::addUint32(buffer, val);
    uint32_t res = 0;
    BufferHelper::readUint24(buffer, res);
    EXPECT_EQ(val, res);
  }
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at Client Capability
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteClientCap) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int client_cap_len = sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), client_cap_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), 0);
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at Extended Client Capability
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteExtClientCap) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), 0);
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at Max Packet
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteMaxPacket) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getBaseClientCap(), mysql_clogin_encode.getBaseClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), 0);
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at Charset
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteCharset) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint16_t) + sizeof(CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) + sizeof(MYSQL_MAX_PACKET);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), 0);
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at Unset bytes
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteUnsetBytes) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  mysql_clogin_encode.setUsername("user1");

  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at username
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteUser) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  mysql_clogin_encode.setUsername("user1");

  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), "");
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at auth data length
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteAuthLen) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());

  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at auth response
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteAuthPasswd) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthPluginData8();
  mysql_clogin_encode.setAuthResp(passwd);

  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA) +
                       sizeof(MYSQL_MAX_PACKET) + sizeof(MYSQL_CHARSET) + UNSET_BYTES +
                       user.length() + 3;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  ;
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at db name
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteDbName) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_CONNECT_WITH_DB);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthPluginData8();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string db = "mysql.db";
  mysql_clogin_encode.setDb(db);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PROTOCOL_41) + sizeof(MYSQL_MAX_PACKET) +
                       sizeof(MYSQL_CHARSET) + UNSET_BYTES + user.length() + 1 + passwd.size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), "");
}

/*
 * Negative Test the MYSQL Client Login 41 message parser:
 * Incomplete header at auth plugin name
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin41IncompleteAuthPluginName) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_PROTOCOL_41 | CLIENT_CONNECT_WITH_DB |
                                   CLIENT_PLUGIN_AUTH);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  std::string user("user1");
  mysql_clogin_encode.setUsername(user);
  std::string passwd = MySQLTestUtils::getAuthPluginData8();
  mysql_clogin_encode.setAuthResp(passwd);
  std::string db = "mysql.db";
  mysql_clogin_encode.setDb(db);
  mysql_clogin_encode.setAuthPluginName(MySQLTestUtils::getAuthPluginName());
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(CLIENT_PROTOCOL_41) + sizeof(MYSQL_MAX_PACKET) +
                       sizeof(MYSQL_CHARSET) + UNSET_BYTES + user.length() + 1 + passwd.size() + 1 +
                       db.size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getExtendedClientCap(), mysql_clogin_encode.getExtendedClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), mysql_clogin_encode.getDb());
  EXPECT_EQ(mysql_clogin_decode.getAuthPluginName(), "");
}

/*
 * Negative Test the MYSQL Client 320 login message parser:
 * Incomplete header at cap
 */
TEST_F(MySQLCLoginTest, MySQLClient320LoginIncompleteClientCap) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_CONNECT_WITH_DB);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = 0;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), 0);
}

/*
 * Negative Test the MYSQL Client 320 login message parser:
 * Incomplete max packet
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin320IncompleteMaxPacketSize) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setExtendedClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), 0);
}

/*
 * Negative Test the MYSQL Client login 320 message parser:
 * Incomplete username
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin320IncompleteUsername) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setUsername("user");
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t);
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), "");
}

/*
 * Negative Test the MYSQL Client login 320 message parser:
 * Incomplete auth response
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin320IncompleteAuthResp) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(0);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setUsername("user");
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t) +
                       mysql_clogin_encode.getUsername().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client login 320 with CLIENT_CONNECT_WITH_DB message parser:
 * Incomplete auth response
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin320WithDbIncompleteAuthResp) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_CONNECT_WITH_DB);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setUsername("user");
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len =
      sizeof(uint16_t) + sizeof(uint8_t) * 3 + mysql_clogin_encode.getUsername().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), "");
}

/*
 * Negative Test the MYSQL Client login 320 with CLIENT_CONNECT_WITH_DB message parser:
 * Incomplete db name
 */
TEST_F(MySQLCLoginTest, MySQLClientLogin320WithDbIncompleteDb) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_CONNECT_WITH_DB);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setUsername("user");
  mysql_clogin_encode.setAuthResp(MySQLTestUtils::getAuthResp8());
  mysql_clogin_encode.setDb(MySQLTestUtils::getDb());
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(uint16_t) + sizeof(uint8_t) * 3 +
                       mysql_clogin_encode.getUsername().size() + 1 +
                       mysql_clogin_encode.getAuthResp().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getUsername(), mysql_clogin_encode.getUsername());
  EXPECT_EQ(mysql_clogin_decode.getAuthResp(), mysql_clogin_encode.getAuthResp());
  EXPECT_EQ(mysql_clogin_decode.getDb(), "");
}

/*
 * Test the MYSQL Client Login SSL message parser:
 * - message is encoded using the ClientLogin class
 * - message is decoded using the ClientLogin class
 */
TEST_F(MySQLCLoginTest, MySQLClientLoginSSLEncDec) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_SSL | CLIENT_PROTOCOL_41 | CLIENT_PLUGIN_AUTH);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  Buffer::OwnedImpl decode_data;
  mysql_clogin_encode.encode(decode_data);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.isSSLRequest(), true);
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
}

/*
 * Negative Test the MYSQL Client login SSL message parser:
 * Incomplete cap flag
 */
TEST_F(MySQLCLoginTest, MySQLClientLoginSslIncompleteCap) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_SSL);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = 0;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), 0);
}

/*
 * Negative Test the MYSQL Client login SSL message parser:
 * Incomplete max packet
 */
TEST_F(MySQLCLoginTest, MySQLClientLoginSslIncompleteMaxPacket) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_SSL);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(mysql_clogin_encode.getClientCap());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), 0);
}

/*
 * Negative Test the MYSQL Client login SSL message parser:
 * Incomplete character set
 */
TEST_F(MySQLCLoginTest, MySQLClientLoginSslIncompleteCharset) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_SSL);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_clogin_encode.getClientCap()) + sizeof(mysql_clogin_encode.getMaxPacket());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), 0);
}

/*
 * Negative Test the MYSQL Client login SSL message parser:
 * Incomplete reserved
 */
TEST_F(MySQLCLoginTest, MySQLClientLoginSslIncompleteReserved) {
  ClientLogin mysql_clogin_encode{};
  mysql_clogin_encode.setClientCap(CLIENT_SSL);
  mysql_clogin_encode.setMaxPacket(MYSQL_MAX_PACKET);
  mysql_clogin_encode.setCharset(MYSQL_CHARSET);
  Buffer::OwnedImpl buffer;
  mysql_clogin_encode.encode(buffer);

  int incomplete_len = sizeof(mysql_clogin_encode.getClientCap()) +
                       sizeof(mysql_clogin_encode.getMaxPacket()) +
                       sizeof(mysql_clogin_encode.getCharset());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ClientLogin mysql_clogin_decode{};
  mysql_clogin_decode.decode(decode_data, CHALLENGE_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_clogin_decode.getClientCap(), mysql_clogin_encode.getClientCap());
  EXPECT_EQ(mysql_clogin_decode.getMaxPacket(), mysql_clogin_encode.getMaxPacket());
  EXPECT_EQ(mysql_clogin_decode.getCharset(), mysql_clogin_encode.getCharset());
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
