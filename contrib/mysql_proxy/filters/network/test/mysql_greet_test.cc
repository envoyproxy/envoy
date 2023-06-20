#include "source/common/buffer/buffer_impl.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_codec_greeting.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class MySQLGreetTest : public testing::Test {
public:
  ServerGreeting getV9Greet() const {
    ServerGreeting mysql_greet_encode{};
    mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_9);
    mysql_greet_encode.setVersion(MySQLTestUtils::getVersion());
    mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
    mysql_greet_encode.setAuthPluginData(MySQLTestUtils::getAuthPluginData8());
    return mysql_greet_encode;
  }
  ServerGreeting getV10Greet(uint16_t base_cap, uint16_t ext_cap) const {
    ServerGreeting mysql_greet_encode{};
    mysql_greet_encode.setProtocol(MYSQL_PROTOCOL_10);
    mysql_greet_encode.setVersion(MySQLTestUtils::getVersion());
    mysql_greet_encode.setThreadId(MYSQL_THREAD_ID);
    mysql_greet_encode.setAuthPluginData(MySQLTestUtils::getAuthPluginData20());
    mysql_greet_encode.setBaseServerCap(base_cap);
    mysql_greet_encode.setExtServerCap(ext_cap);
    mysql_greet_encode.setServerStatus(MYSQL_SERVER_STATUS);
    mysql_greet_encode.setServerCharset(MYSQL_CHARSET);
    mysql_greet_encode.setAuthPluginName(MySQLTestUtils::getAuthPluginName());
    return mysql_greet_encode;
  }
  ServerGreeting getV10Greet(uint32_t cap) const { return getV10Greet(cap & 0xffff, cap >> 16); }
};

TEST_F(MySQLGreetTest, MySQLServerChallengeV9EncDec) {
  ServerGreeting mysql_greet_encode = getV9Greet();
  Buffer::OwnedImpl data;
  mysql_greet_encode.encode(data);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(data, GREETING_SEQ_NUM, data.length());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
}

/*
 * Test the MYSQL Greeting message V10:
 * - message is encoded using the ServerGreeting class
 * - message is decoded using the ServerGreeting class
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeV10EncDec) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  Buffer::OwnedImpl decode_data;
  mysql_greet_encode.encode(decode_data);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2().size(), 0);
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginName(), "");
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete protocol
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteProtocol) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len = 0;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), 0);
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete version
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteVersion) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len = sizeof(mysql_greet_encode.getProtocol());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), "");
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete thread_id
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteThreadId) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), 0);
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete auth_plugin_data
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteSalt) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len = sizeof(mysql_greet_encode.getProtocol()) +
                       mysql_greet_encode.getVersion().size() + 1 +
                       sizeof(mysql_greet_encode.getThreadId());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData().size(), 0);
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete filler
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteFiller) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size();
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete Server Capabilities
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteServerCap) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAPAB, 0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), 0);
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete Server Charset
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteServerCharset) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAPAB, 0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), 0);
}

/*
 * Negative Testing: Server Greetings Incomplete
 * - incomplete Server Status
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteServerStatus) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAPAB, 0);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), 0);
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete extended Server Capabilities
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteExtServerCap) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAPAB, MYSQL_SERVER_EXT_CAPAB);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), 0);
}

/*
 * Negative Testing: Server Greetings v10 Incomplete
 * - incomplete extended Server Capabilities
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeIncompleteAuthPluginDataLen) {
  ServerGreeting mysql_greet_encode = getV10Greet(0);
  mysql_greet_encode.setAuthPluginData1(MySQLTestUtils::getAuthPluginData8());
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap());
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), 0);
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with auth plugin data flag
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerCapAuthPlugin) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAP_AUTH_PLUGIN);

  Buffer::OwnedImpl decode_data;
  mysql_greet_encode.encode(decode_data);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2(), mysql_greet_encode.getAuthPluginData2());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginName(), mysql_greet_encode.getAuthPluginName());
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with auth plugin data flag incomplete
 * - incomplete of reserved bytes
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerAuthPluginInCompleteReserved) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAP_AUTH_PLUGIN);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap()) + 1;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2().size(), 0);
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());

  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginName(), "");
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with auth plugin data flag incomplete
 * - incomplete of auth-plugin-data2
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerAuthPluginInCompleteAuthData2) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAP_AUTH_PLUGIN);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap()) + 1 + 10;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2().size(), 0);
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());

  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with auth plugin data flag incomplete
 * - incomplete of skipped bytes
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerAuthPluginInCompleteSkippedBytes) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAP_AUTH_PLUGIN);
  mysql_greet_encode.setAuthPluginData2(MySQLTestUtils::getAuthPluginData8());
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap()) + 1 + 10 +
      mysql_greet_encode.getAuthPluginData2().size();
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());

  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2(), mysql_greet_encode.getAuthPluginData2());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginName(), "");
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with auth plugin data flag incomplete
 * - incomplete of auth plugin name
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerAuthPluginInCompleteAuthPluginName) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_CAP_AUTH_PLUGIN);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap()) + 1 + 10 +
      mysql_greet_encode.getAuthPluginData2().size();
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2(), mysql_greet_encode.getAuthPluginData2());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginName(), "");
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with security connection flag
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerCapSecurityConnection) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_SECURE_CONNECTION);

  Buffer::OwnedImpl decode_data;
  mysql_greet_encode.encode(decode_data);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData(), mysql_greet_encode.getAuthPluginData());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2(), mysql_greet_encode.getAuthPluginData2());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());
  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with security connection flag
 * - incomplete of auth-plugin-data2
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerSecurityConnectionInCompleteData2) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_SECURE_CONNECTION);
  mysql_greet_encode.setServerStatus(MYSQL_SERVER_STATUS);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap()) + 1 + 10;
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2().size(), 0);
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());

  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
}

/*
 * Testing: Server Greetings Protocol 10 Server Capabilities with security connection flag
 * - incomplete of eof
 */
TEST_F(MySQLGreetTest, MySQLServerChallengeP10ServerSecurityConnectionIncompleteEof) {
  ServerGreeting mysql_greet_encode = getV10Greet(MYSQL_SERVER_SECURE_CONNECTION);
  mysql_greet_encode.setServerStatus(MYSQL_SERVER_STATUS);
  Buffer::OwnedImpl buffer;
  mysql_greet_encode.encode(buffer);

  int incomplete_len =
      sizeof(mysql_greet_encode.getProtocol()) + mysql_greet_encode.getVersion().size() + 1 +
      sizeof(mysql_greet_encode.getThreadId()) + mysql_greet_encode.getAuthPluginData1().size() +
      1 + sizeof(mysql_greet_encode.getBaseServerCap()) +
      sizeof(mysql_greet_encode.getServerCharset()) + sizeof(mysql_greet_encode.getServerStatus()) +
      sizeof(mysql_greet_encode.getExtServerCap()) + 1 + 10 +
      mysql_greet_encode.getAuthPluginData2().size();
  Buffer::OwnedImpl decode_data(buffer.toString().data(), incomplete_len);

  ServerGreeting mysql_greet_decode{};
  mysql_greet_decode.decode(decode_data, GREETING_SEQ_NUM, decode_data.length());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData1(), mysql_greet_encode.getAuthPluginData1());
  EXPECT_EQ(mysql_greet_decode.getAuthPluginData2(), mysql_greet_encode.getAuthPluginData2());
  EXPECT_EQ(mysql_greet_decode.getVersion(), mysql_greet_encode.getVersion());
  EXPECT_EQ(mysql_greet_decode.getProtocol(), mysql_greet_encode.getProtocol());
  EXPECT_EQ(mysql_greet_decode.getThreadId(), mysql_greet_encode.getThreadId());
  EXPECT_EQ(mysql_greet_decode.getServerCharset(), mysql_greet_encode.getServerCharset());

  EXPECT_EQ(mysql_greet_decode.getServerStatus(), mysql_greet_encode.getServerStatus());
  EXPECT_EQ(mysql_greet_decode.getServerCap(), mysql_greet_encode.getServerCap());
  EXPECT_EQ(mysql_greet_decode.getBaseServerCap(), mysql_greet_encode.getBaseServerCap());
  EXPECT_EQ(mysql_greet_decode.getExtServerCap(), mysql_greet_encode.getExtServerCap());
}
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
