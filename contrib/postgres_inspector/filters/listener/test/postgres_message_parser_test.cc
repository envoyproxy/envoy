#include "test/test_common/utility.h"

#include "contrib/postgres_inspector/filters/listener/source/postgres_message_parser.h"
#include "contrib/postgres_inspector/filters/listener/test/postgres_test_utils.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {
namespace {

class PostgresMessageParserTest : public testing::Test {};

TEST_F(PostgresMessageParserTest, IsSslRequestValid) {
  auto buffer = PostgresTestUtils::createSslRequest();
  EXPECT_TRUE(PostgresMessageParser::isSslRequest(buffer, 0));
}

TEST_F(PostgresMessageParserTest, IsSslRequestInvalidCode) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(SSL_REQUEST_MESSAGE_SIZE); // Correct length
  buffer.writeBEInt<uint32_t>(12345);                    // Wrong code
  EXPECT_FALSE(PostgresMessageParser::isSslRequest(buffer, 0));
}

TEST_F(PostgresMessageParserTest, IsSslRequestInvalidLength) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(SSL_REQUEST_MESSAGE_SIZE + 4); // Wrong length
  buffer.writeBEInt<uint32_t>(SSL_REQUEST_CODE);             // Correct code
  EXPECT_FALSE(PostgresMessageParser::isSslRequest(buffer, 0));
}

TEST_F(PostgresMessageParserTest, IsSslRequestInsufficientData) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(SSL_REQUEST_MESSAGE_SIZE); // Only 4 bytes
  EXPECT_FALSE(PostgresMessageParser::isSslRequest(buffer, 0));
}

TEST_F(PostgresMessageParserTest, IsSslRequestWithOffset) {
  Buffer::OwnedImpl buffer;
  buffer.add("dummy", 5); // Add some dummy data first
  auto ssl_request = PostgresTestUtils::createSslRequest();
  buffer.move(ssl_request);

  EXPECT_FALSE(PostgresMessageParser::isSslRequest(buffer, 0)); // At offset 0
  EXPECT_TRUE(PostgresMessageParser::isSslRequest(buffer, 5));  // At offset 5
}

TEST_F(PostgresMessageParserTest, ParseValidStartupMessage) {
  std::map<std::string, std::string> params = {
      {"user", "testuser"}, {"database", "testdb"}, {"application_name", "myapp"}};

  auto buffer = PostgresTestUtils::createStartupMessage(params);

  StartupMessage message;
  EXPECT_TRUE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));

  EXPECT_EQ(POSTGRES_PROTOCOL_VERSION, message.protocol_version);
  EXPECT_EQ(3, message.parameters.size());
  EXPECT_EQ("testuser", message.parameters["user"]);
  EXPECT_EQ("testdb", message.parameters["database"]);
  EXPECT_EQ("myapp", message.parameters["application_name"]);
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageMinimal) {
  std::map<std::string, std::string> params = {{"user", "postgres"}};

  auto buffer = PostgresTestUtils::createStartupMessage(params);

  StartupMessage message;
  EXPECT_TRUE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));

  EXPECT_EQ(POSTGRES_PROTOCOL_VERSION, message.protocol_version);
  EXPECT_EQ(1, message.parameters.size());
  EXPECT_EQ("postgres", message.parameters["user"]);
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageEmpty) {
  std::map<std::string, std::string> params = {};

  auto buffer = PostgresTestUtils::createStartupMessage(params);

  StartupMessage message;
  EXPECT_TRUE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));

  EXPECT_EQ(POSTGRES_PROTOCOL_VERSION, message.protocol_version);
  EXPECT_EQ(0, message.parameters.size());
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageInsufficientData) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(100); // Claims 100 bytes
  // Only has 4 bytes

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageTooSmall) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(STARTUP_HEADER_SIZE - 2); // Too small (< header size)
  buffer.writeBEInt<uint32_t>(POSTGRES_PROTOCOL_VERSION);

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageTooLarge) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(2000); // Larger than max allowed
  buffer.writeBEInt<uint32_t>(POSTGRES_PROTOCOL_VERSION);

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1000));
}

TEST_F(PostgresMessageParserTest, IsCancelRequestValid) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(CANCEL_REQUEST_MESSAGE_SIZE);
  buffer.writeBEInt<uint32_t>(CANCEL_REQUEST_CODE);
  buffer.writeBEInt<uint32_t>(123); // pid
  buffer.writeBEInt<uint32_t>(456); // secret
  EXPECT_TRUE(PostgresMessageParser::isCancelRequest(buffer, 0));
}

TEST_F(PostgresMessageParserTest, IsCancelRequestInvalid) {
  Buffer::OwnedImpl wrong_len;
  wrong_len.writeBEInt<uint32_t>(CANCEL_REQUEST_MESSAGE_SIZE - 1);
  wrong_len.writeBEInt<uint32_t>(CANCEL_REQUEST_CODE);
  EXPECT_FALSE(PostgresMessageParser::isCancelRequest(wrong_len, 0));
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageIncomplete) {
  // Create a partial startup message.
  auto buffer = PostgresTestUtils::createPartialStartupMessage(50, 30);

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageWithOffset) {
  std::map<std::string, std::string> params = {{"user", "test"}};

  Buffer::OwnedImpl buffer;
  buffer.add("prefix", 6); // Add prefix
  auto startup = PostgresTestUtils::createStartupMessage(params);
  buffer.move(startup);

  StartupMessage message;
  EXPECT_FALSE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));
  EXPECT_TRUE(PostgresMessageParser::parseStartupMessage(buffer, 6, message, 1024));

  EXPECT_EQ("test", message.parameters["user"]);
}

TEST_F(PostgresMessageParserTest, ParseStartupMessageSpecialCharacters) {
  std::map<std::string, std::string> params = {{"user", "user@domain.com"},
                                               {"database", "test-db_2"},
                                               {"application_name", "My App (v1.0)"}};

  auto buffer = PostgresTestUtils::createStartupMessage(params);

  StartupMessage message;
  EXPECT_TRUE(PostgresMessageParser::parseStartupMessage(buffer, 0, message, 1024));

  EXPECT_EQ("user@domain.com", message.parameters["user"]);
  EXPECT_EQ("test-db_2", message.parameters["database"]);
  EXPECT_EQ("My App (v1.0)", message.parameters["application_name"]);
}

TEST_F(PostgresMessageParserTest, HasCompleteMessageTrue) {
  auto buffer = PostgresTestUtils::createStartupMessage({{"user", "test"}});

  uint32_t message_length;
  EXPECT_TRUE(PostgresMessageParser::hasCompleteMessage(buffer, 0, message_length));
  EXPECT_EQ(buffer.length(), message_length);
}

TEST_F(PostgresMessageParserTest, HasCompleteMessageFalse) {
  Buffer::OwnedImpl buffer;
  buffer.writeBEInt<uint32_t>(100); // Claims 100 bytes
  buffer.add("data", 4);            // Only 8 bytes total

  uint32_t message_length;
  EXPECT_FALSE(PostgresMessageParser::hasCompleteMessage(buffer, 0, message_length));
  EXPECT_EQ(100, message_length);
}

TEST_F(PostgresMessageParserTest, HasCompleteMessageInsufficientData) {
  Buffer::OwnedImpl buffer;
  buffer.add("xx", 2); // Only 2 bytes (need 4 for length)

  uint32_t message_length;
  EXPECT_FALSE(PostgresMessageParser::hasCompleteMessage(buffer, 0, message_length));
}

TEST_F(PostgresMessageParserTest, HasCompleteMessageWithOffset) {
  Buffer::OwnedImpl buffer;
  buffer.add("prefix", 6);
  auto startup = PostgresTestUtils::createStartupMessage({{"user", "test"}});
  buffer.move(startup);

  uint32_t message_length;
  EXPECT_FALSE(PostgresMessageParser::hasCompleteMessage(buffer, 0, message_length));
  EXPECT_TRUE(PostgresMessageParser::hasCompleteMessage(buffer, 6, message_length));
}

} // namespace
} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
