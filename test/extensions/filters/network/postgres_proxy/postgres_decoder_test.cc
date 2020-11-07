#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "extensions/filters/network/postgres_proxy/postgres_decoder.h"

#include "test/extensions/filters/network/postgres_proxy/postgres_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class DecoderCallbacksMock : public DecoderCallbacks {
public:
  MOCK_METHOD(void, incMessagesBackend, (), (override));
  MOCK_METHOD(void, incMessagesFrontend, (), (override));
  MOCK_METHOD(void, incMessagesUnknown, (), (override));
  MOCK_METHOD(void, incSessionsEncrypted, (), (override));
  MOCK_METHOD(void, incSessionsUnencrypted, (), (override));
  MOCK_METHOD(void, incStatements, (StatementType), (override));
  MOCK_METHOD(void, incTransactions, (), (override));
  MOCK_METHOD(void, incTransactionsCommit, (), (override));
  MOCK_METHOD(void, incTransactionsRollback, (), (override));
  MOCK_METHOD(void, incNotices, (NoticeType), (override));
  MOCK_METHOD(void, incErrors, (ErrorType), (override));
  MOCK_METHOD(void, processQuery, (const std::string&), (override));
};

// Define fixture class with decoder and mock callbacks.
class PostgresProxyDecoderTestBase {
public:
  PostgresProxyDecoderTestBase() {
    decoder_ = std::make_unique<DecoderImpl>(&callbacks_);
    decoder_->initialize();
    decoder_->setStartup(false);
  }

protected:
  ::testing::NiceMock<DecoderCallbacksMock> callbacks_;
  std::unique_ptr<DecoderImpl> decoder_;

  // fields often used
  Buffer::OwnedImpl data_;
  char buf_[256]{};
  std::string payload_;
};

class PostgresProxyDecoderTest : public PostgresProxyDecoderTestBase, public ::testing::Test {};

// Class is used for parameterized tests for frontend messages.
class PostgresProxyFrontendDecoderTest : public PostgresProxyDecoderTestBase,
                                         public ::testing::TestWithParam<std::string> {};

// Class is used for parameterized tests for encrypted messages.
class PostgresProxyFrontendEncrDecoderTest : public PostgresProxyDecoderTestBase,
                                             public ::testing::TestWithParam<uint32_t> {};

// Class is used for parameterized tests for backend messages.
class PostgresProxyBackendDecoderTest : public PostgresProxyDecoderTestBase,
                                        public ::testing::TestWithParam<std::string> {};

class PostgresProxyErrorTest
    : public PostgresProxyDecoderTestBase,
      public ::testing::TestWithParam<std::tuple<std::string, DecoderCallbacks::ErrorType>> {};

class PostgresProxyNoticeTest
    : public PostgresProxyDecoderTestBase,
      public ::testing::TestWithParam<std::tuple<std::string, DecoderCallbacks::NoticeType>> {};

// Test processing the startup message from a client.
// For historical reasons, the first message does not include
// command (first byte). It starts with length. The startup
// message contains the protocol version. After processing the
// startup message the server should start using message format
// with command as 1st byte.
TEST_F(PostgresProxyDecoderTest, StartupMessage) {
  decoder_->setStartup(true);

  buf_[0] = '\0';
  // Startup message has the following structure:
  // Length (4 bytes) - payload and length field
  // version (4 bytes)
  // Attributes: key/value pairs separated by '\0'
  data_.writeBEInt<uint32_t>(53);
  // Add version code
  data_.writeBEInt<uint32_t>(0x00030000);
  // user-postgres key-pair
  data_.add("user"); // 4 bytes
  data_.add(buf_, 1);
  data_.add("postgres"); // 8 bytes
  data_.add(buf_, 1);
  // database-test-db key-pair
  data_.add("database"); // 8 bytes
  data_.add(buf_, 1);
  data_.add("testdb"); // 6 bytes
  data_.add(buf_, 1);
  // Some other attribute
  data_.add("attribute"); // 9 bytes
  data_.add(buf_, 1);
  data_.add("blah"); // 4 bytes
  data_.add(buf_, 1);
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
  // Verify parsing attributes
  ASSERT_THAT(decoder_->getAttributes().at("user"), "postgres");
  ASSERT_THAT(decoder_->getAttributes().at("database"), "testdb");
  // This attribute should not be found
  ASSERT_THAT(decoder_->getAttributes().find("no"), decoder_->getAttributes().end());

  // Now feed normal message with 1bytes as command.
  data_.add("P");
  // Add length.
  data_.writeBEInt<uint32_t>(6); // 4 bytes of length + 2 bytes of data.
  data_.add("AB");
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

// Test verifies that when Startup message does not carry
// "database" attribute, it is derived from "user".
TEST_F(PostgresProxyDecoderTest, StartupMessageNoAttr) {
  decoder_->setStartup(true);

  buf_[0] = '\0';
  // Startup message has the following structure:
  // Length (4 bytes) - payload and length field
  // version (4 bytes)
  // Attributes: key/value pairs separated by '\0'
  data_.writeBEInt<uint32_t>(37);
  // Add version code
  data_.writeBEInt<uint32_t>(0x00030000);
  // user-postgres key-pair
  data_.add("user"); // 4 bytes
  data_.add(buf_, 1);
  data_.add("postgres"); // 8 bytes
  data_.add(buf_, 1);
  // database-test-db key-pair
  // Some other attribute
  data_.add("attribute"); // 9 bytes
  data_.add(buf_, 1);
  data_.add("blah"); // 4 bytes
  data_.add(buf_, 1);
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);

  // Verify parsing attributes
  ASSERT_THAT(decoder_->getAttributes().at("user"), "postgres");
  ASSERT_THAT(decoder_->getAttributes().at("database"), "postgres");
  // This attribute should not be found
  ASSERT_THAT(decoder_->getAttributes().find("no"), decoder_->getAttributes().end());
}

// Test processing messages which map 1:1 with buffer.
// The buffer contains just a single entire message and
// nothing more.
TEST_F(PostgresProxyDecoderTest, ReadingBufferSingleMessages) {

  // Feed empty buffer - should not crash.
  decoder_->onData(data_, true);

  // Put one byte. This is not enough to parse the message and that byte
  // should stay in the buffer.
  data_.add("P");
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 1);

  // Add length of 4 bytes. It would mean completely empty message.
  // but it should be consumed.
  data_.writeBEInt<uint32_t>(4);
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);

  // Create a message with 5 additional bytes.
  data_.add("P");
  // Add length.
  data_.writeBEInt<uint32_t>(9); // 4 bytes of length field + 5 of data.
  data_.add(buf_, 5);
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

// Test simulates situation when decoder is called with incomplete message.
// The message should not be processed until the buffer is filled
// with missing bytes.
TEST_F(PostgresProxyDecoderTest, ReadingBufferLargeMessages) {
  // Fill the buffer with message of 100 bytes long
  // but the buffer contains only 98 bytes.
  // It should not be processed.
  data_.add("P");
  // Add length.
  data_.writeBEInt<uint32_t>(100); // This also includes length field
  data_.add(buf_, 94);
  decoder_->onData(data_, true);
  // The buffer contains command (1 byte), length (4 bytes) and 94 bytes of message.
  ASSERT_THAT(data_.length(), 99);

  // Add 2 missing bytes and feed again to decoder.
  data_.add("AB");
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

// Test simulates situation when a buffer contains more than one
// message. Call to the decoder should consume only one message
// at a time and only when the buffer contains the entire message.
TEST_F(PostgresProxyDecoderTest, TwoMessagesInOneBuffer) {
  // Create the first message of 50 bytes long (+1 for command).
  data_.add("P");
  // Add length.
  data_.writeBEInt<uint32_t>(50);
  data_.add(buf_, 46);

  // Create the second message of 50 + 46 bytes (+1 for command).
  data_.add("P");
  // Add length.
  data_.writeBEInt<uint32_t>(96);
  data_.add(buf_, 46);
  data_.add(buf_, 46);

  // The buffer contains two messaged:
  // 1st: command (1 byte), length (4 bytes), 46 bytes of data
  // 2nd: command (1 byte), length (4 bytes), 92 bytes of data
  ASSERT_THAT(data_.length(), 148);
  // Process the first message.
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 97);
  // Process the second message.
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

TEST_F(PostgresProxyDecoderTest, Unknown) {
  // Create invalid message. The first byte is invalid "="
  // Message must be at least 5 bytes to be parsed.
  EXPECT_CALL(callbacks_, incMessagesUnknown()).Times(1);
  createPostgresMsg(data_, "=", "some not important string which will be ignored anyways");
  decoder_->onData(data_, true);
}

// Test if each frontend command calls incMessagesFrontend() method.
TEST_P(PostgresProxyFrontendDecoderTest, FrontendInc) {
  EXPECT_CALL(callbacks_, incMessagesFrontend()).Times(1);
  createPostgresMsg(data_, GetParam(), "SELECT 1;");
  decoder_->onData(data_, true);

  // Make sure that decoder releases memory used during message processing.
  ASSERT_TRUE(decoder_->getMessage().empty());
}

// Run the above test for each frontend message.
INSTANTIATE_TEST_SUITE_P(FrontEndMessagesTests, PostgresProxyFrontendDecoderTest,
                         ::testing::Values("B", "C", "d", "c", "f", "D", "E", "H", "F", "p", "P",
                                           "p", "Q", "S", "X"));

// Test if X message triggers incRollback and sets proper state in transaction.
TEST_F(PostgresProxyFrontendDecoderTest, TerminateMessage) {
  // Set decoder state NOT to be in_transaction.
  decoder_->getSession().setInTransaction(false);
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(0);
  createPostgresMsg(data_, "X");
  decoder_->onData(data_, true);

  // Now set the decoder to be in_transaction state.
  decoder_->getSession().setInTransaction(true);
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(1);
  createPostgresMsg(data_, "X");
  decoder_->onData(data_, true);
  ASSERT_FALSE(decoder_->getSession().inTransaction());
}

// Query message should invoke filter's callback message
TEST_F(PostgresProxyFrontendDecoderTest, QueryMessage) {
  EXPECT_CALL(callbacks_, processQuery).Times(1);
  createPostgresMsg(data_, "Q", "SELECT * FROM whatever;");
  decoder_->onData(data_, true);
}

// Parse message has optional Query name which may be in front of actual
// query statement. This test verifies that both formats are processed
// correctly.
TEST_F(PostgresProxyFrontendDecoderTest, ParseMessage) {
  std::string query = "SELECT * FROM whatever;";
  std::string query_name, query_params;

  // Should be called twice with the same query.
  EXPECT_CALL(callbacks_, processQuery(query)).Times(2);

  // Set params to be zero.
  query_params.reserve(2);
  query_params += '\0';
  query_params += '\0';

  // Message without optional query name.
  query_name.reserve(1);
  query_name += '\0';
  createPostgresMsg(data_, "P", query_name + query + query_params);
  decoder_->onData(data_, true);

  // Message with optional name query_name
  query_name.clear();
  query_name.reserve(5);
  query_name += "P0_8";
  query_name += '\0';
  createPostgresMsg(data_, "P", query_name + query + query_params);
  decoder_->onData(data_, true);
}

// Test if each backend command calls incMessagesBackend()) method.
TEST_P(PostgresProxyBackendDecoderTest, BackendInc) {
  EXPECT_CALL(callbacks_, incMessagesBackend()).Times(1);
  createPostgresMsg(data_, GetParam(), "Some not important message");
  decoder_->onData(data_, false);
}

// Run the above test for each backend message.
INSTANTIATE_TEST_SUITE_P(BackendMessagesTests, PostgresProxyBackendDecoderTest,
                         ::testing::Values("R", "K", "2", "3", "C", "d", "c", "G", "H", "D", "I",
                                           "E", "V", "v", "n", "N", "A", "t", "S", "1", "s", "Z",
                                           "T"));
// Test parsing backend messages.
// The parser should react only to the first word until the space.
TEST_F(PostgresProxyBackendDecoderTest, ParseStatement) {
  // Payload contains a space after the keyword
  // Rollback counter should be bumped up.
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  createPostgresMsg(data_, "C", "ROLLBACK 123");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Now try just keyword without a space at the end.
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  createPostgresMsg(data_, "C", "ROLLBACK");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Partial message should be ignored.
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(0);
  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Other));
  createPostgresMsg(data_, "C", "ROLL");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Keyword without a space  should be ignored.
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(0);
  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Other));
  createPostgresMsg(data_, "C", "ROLLBACK123");
  decoder_->onData(data_, false);
  data_.drain(data_.length());
}

// Test Backend messages and make sure that they
// trigger proper stats updates.
TEST_F(PostgresProxyDecoderTest, Backend) {
  // C message
  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Other));
  createPostgresMsg(data_, "C", "BEGIN 123");
  decoder_->onData(data_, false);
  data_.drain(data_.length());
  ASSERT_TRUE(decoder_->getSession().inTransaction());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Other));
  createPostgresMsg(data_, "C", "START TR");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Noop));
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  createPostgresMsg(data_, "C", "COMMIT");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Select));
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  createPostgresMsg(data_, "C", "SELECT");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Noop));
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  createPostgresMsg(data_, "C", "ROLLBACK");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Insert));
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  createPostgresMsg(data_, "C", "INSERT 1");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Update));
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  createPostgresMsg(data_, "C", "UPDATE 123");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements(DecoderCallbacks::StatementType::Delete));
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  createPostgresMsg(data_, "C", "DELETE 88");
  decoder_->onData(data_, false);
  data_.drain(data_.length());
}

// Test checks deep inspection of the R message.
// During login/authentication phase client and server exchange
// multiple R messages. Only payload with length is 8 and
// payload with uint32 number equal to 0 indicates
// successful authentication.
TEST_F(PostgresProxyBackendDecoderTest, AuthenticationMsg) {
  // Create authentication message which does not
  // mean that authentication was OK. The number of
  // sessions must not be increased.
  EXPECT_CALL(callbacks_, incSessionsUnencrypted()).Times(0);
  createPostgresMsg(data_, "R", "blah blah");
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Create the correct payload which means that
  // authentication completed successfully.
  EXPECT_CALL(callbacks_, incSessionsUnencrypted());
  data_.add("R");
  // Add length.
  data_.writeBEInt<uint32_t>(8);
  // Add 4-byte code.
  data_.writeBEInt<uint32_t>(0);
  decoder_->onData(data_, false);
  data_.drain(data_.length());
}

// Test check parsing of E message. The message
// indicates error.
TEST_P(PostgresProxyErrorTest, ParseErrorMsgs) {
  EXPECT_CALL(callbacks_, incErrors(std::get<1>(GetParam())));
  createPostgresMsg(data_, "E", std::get<0>(GetParam()));
  decoder_->onData(data_, false);
}

INSTANTIATE_TEST_SUITE_P(
    PostgresProxyErrorTestSuite, PostgresProxyErrorTest,
    ::testing::Values(
        std::make_tuple("blah blah", DecoderCallbacks::ErrorType::Unknown),
        std::make_tuple("SERRORC1234", DecoderCallbacks::ErrorType::Error),
        std::make_tuple("SERRORVERRORC1234", DecoderCallbacks::ErrorType::Error),
        std::make_tuple("SFATALVFATALC22012", DecoderCallbacks::ErrorType::Fatal),
        std::make_tuple("SPANICVPANICC22012", DecoderCallbacks::ErrorType::Panic),
        // This is the real German message in Postgres > 9.6. It contains keyword
        // in English with V prefix.
        std::make_tuple("SPANIKVPANICC42501Mkonnte Datei »pg_wal/000000010000000100000096« nicht "
                        "öffnen: Permission deniedFxlog.cL3229RXLogFileInit",
                        DecoderCallbacks::ErrorType::Panic),
        // This is German message indicating error. The comment field contains word PANIC.
        // Since we do not decode other languages, it should go into Other bucket.
        // This situation can only happen in Postgres < 9.6. Starting with version 9.6
        // messages must have severity in English with prefix V.
        std::make_tuple("SFEHLERCP0001MMy PANIC ugly messageFpl_exec.cL3216Rexec_stmt_raise",
                        DecoderCallbacks::ErrorType::Unknown)));

// Test parsing N message. It indicate notice
// and carries additional information about the
// purpose of the message.
TEST_P(PostgresProxyNoticeTest, ParseNoticeMsgs) {
  EXPECT_CALL(callbacks_, incNotices(std::get<1>(GetParam())));
  createPostgresMsg(data_, "N", std::get<0>(GetParam()));
  decoder_->onData(data_, false);
}

INSTANTIATE_TEST_SUITE_P(
    PostgresProxyNoticeTestSuite, PostgresProxyNoticeTest,
    ::testing::Values(std::make_tuple("blah blah", DecoderCallbacks::NoticeType::Unknown),
                      std::make_tuple("SblalalaC2345", DecoderCallbacks::NoticeType::Unknown),
                      std::make_tuple("SblahVWARNING23345", DecoderCallbacks::NoticeType::Warning),
                      std::make_tuple("SNOTICEERRORbbal4", DecoderCallbacks::NoticeType::Notice),
                      std::make_tuple("SINFOVblabla", DecoderCallbacks::NoticeType::Info),
                      std::make_tuple("SDEBUGDEBUG", DecoderCallbacks::NoticeType::Debug),
                      std::make_tuple("SLOGGGGINFO", DecoderCallbacks::NoticeType::Log)));

// Test checks if the decoder can detect initial message which indicates
// that protocol uses encryption.
TEST_P(PostgresProxyFrontendEncrDecoderTest, EncyptedTraffic) {
  // Set decoder to wait for initial message.
  decoder_->setStartup(true);

  // Initial state is no-encryption.
  ASSERT_FALSE(decoder_->encrypted());

  // Create SSLRequest.
  EXPECT_CALL(callbacks_, incSessionsEncrypted());
  // Add length.
  data_.writeBEInt<uint32_t>(8);
  // 1234 in the most significant 16 bits, and some code in the least significant 16 bits.
  // Add 4 bytes long code
  data_.writeBEInt<uint32_t>(GetParam());
  decoder_->onData(data_, false);
  ASSERT_TRUE(decoder_->encrypted());
  // Decoder should drain data.
  ASSERT_THAT(data_.length(), 0);

  // Now when decoder detected encrypted traffic is should not
  // react to any messages (even not encrypted ones).
  EXPECT_CALL(callbacks_, incMessagesFrontend()).Times(0);

  createPostgresMsg(data_, "P", "Some message just to fill the payload.");
  decoder_->onData(data_, true);
  // Decoder should drain data.
  ASSERT_THAT(data_.length(), 0);
}

// Run encryption tests.
// 80877103 is SSL code
// 80877104 is GSS code
INSTANTIATE_TEST_SUITE_P(FrontendEncryptedMessagesTests, PostgresProxyFrontendEncrDecoderTest,
                         ::testing::Values(80877103, 80877104));

class FakeBuffer : public Buffer::Instance {
public:
  MOCK_METHOD(void, addDrainTracker, (std::function<void()>), (override));
  MOCK_METHOD(void, add, (const void*, uint64_t), (override));
  MOCK_METHOD(void, addBufferFragment, (Buffer::BufferFragment&), (override));
  MOCK_METHOD(void, add, (absl::string_view), (override));
  MOCK_METHOD(void, add, (const Instance&), (override));
  MOCK_METHOD(void, prepend, (absl::string_view), (override));
  MOCK_METHOD(void, prepend, (Instance&), (override));
  MOCK_METHOD(void, commit, (Buffer::RawSlice*, uint64_t), (override));
  MOCK_METHOD(void, copyOut, (size_t, uint64_t, void*), (const, override));
  MOCK_METHOD(void, drain, (uint64_t), (override));
  MOCK_METHOD(Buffer::RawSliceVector, getRawSlices, (absl::optional<uint64_t>), (const, override));
  MOCK_METHOD(Buffer::SliceDataPtr, extractMutableFrontSlice, (), (override));
  MOCK_METHOD(uint64_t, length, (), (const, override));
  MOCK_METHOD(void*, linearize, (uint32_t), (override));
  MOCK_METHOD(void, move, (Instance&), (override));
  MOCK_METHOD(void, move, (Instance&, uint64_t), (override));
  MOCK_METHOD(uint64_t, reserve, (uint64_t, Buffer::RawSlice*, uint64_t), (override));
  MOCK_METHOD(ssize_t, search, (const void*, uint64_t, size_t, size_t), (const, override));
  MOCK_METHOD(bool, startsWith, (absl::string_view), (const, override));
  MOCK_METHOD(std::string, toString, (), (const, override));
};

// Test verifies that decoder calls Buffer::linearize method
// for messages which have associated 'action'.
TEST_F(PostgresProxyDecoderTest, Linearize) {
  testing::NiceMock<FakeBuffer> fake_buf;
  uint8_t body[] = "test\0";

  decoder_->setStartup(false);

  // Simulate that decoder reads message which needs processing.
  // Query 'Q' message's body is just string.
  // Message header is 5 bytes and body will contain string "test\0".
  EXPECT_CALL(fake_buf, length).WillRepeatedly(testing::Return(10));
  // The decoder will first ask for 1-byte message type
  // Then for length and finally for message body.
  EXPECT_CALL(fake_buf, copyOut)
      .WillOnce([](size_t start, uint64_t size, void* data) {
        ASSERT_THAT(start, 0);
        ASSERT_THAT(size, 1);
        *(static_cast<char*>(data)) = 'Q';
      })
      .WillOnce([](size_t start, uint64_t size, void* data) {
        ASSERT_THAT(start, 1);
        ASSERT_THAT(size, 4);
        *(static_cast<uint32_t*>(data)) = htonl(9);
      })
      .WillRepeatedly([=](size_t start, uint64_t size, void* data) {
        ASSERT_THAT(start, 0);
        ASSERT_THAT(size, 5);
        memcpy(data, body, 5);
      });

  // It should call "Buffer::linearize".
  EXPECT_CALL(fake_buf, linearize).WillOnce([&](uint32_t) -> void* { return body; });

  decoder_->onData(fake_buf, false);

  // Simulate that decoder reads message which does not need processing.
  // BindComplete message has type '2' and empty body.
  // Total message length is equal to length of header (5 bytes).
  EXPECT_CALL(fake_buf, length).WillRepeatedly(testing::Return(5));
  // The decoder will first ask for 1-byte message type and next for length.
  EXPECT_CALL(fake_buf, copyOut)
      .WillOnce([](size_t start, uint64_t size, void* data) {
        ASSERT_THAT(start, 0);
        ASSERT_THAT(size, 1);
        *(static_cast<char*>(data)) = '2';
      })
      .WillOnce([](size_t start, uint64_t size, void* data) {
        ASSERT_THAT(start, 1);
        ASSERT_THAT(size, 4);
        *(static_cast<uint32_t*>(data)) = htonl(4);
      });

  // Make sure that decoder does not call linearize.
  EXPECT_CALL(fake_buf, linearize).Times(0);

  decoder_->onData(fake_buf, false);
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
