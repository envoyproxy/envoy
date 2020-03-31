#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

class DecoderCallbacksMock : public DecoderCallbacks {
public:
  MOCK_METHOD(void, incBackend, (), (override));
  MOCK_METHOD(void, incFrontend, (), (override));
  MOCK_METHOD(void, incErrorsError, (), (override));
  MOCK_METHOD(void, incErrorsFatal, (), (override));
  MOCK_METHOD(void, incErrorsPanic, (), (override));
  MOCK_METHOD(void, incErrorsUnknown, (), (override));
  MOCK_METHOD(void, incSessionsEncrypted, (), (override));
  MOCK_METHOD(void, incSessionsUnencrypted, (), (override));
  MOCK_METHOD(void, incStatements, (), (override));
  MOCK_METHOD(void, incStatementsDelete, (), (override));
  MOCK_METHOD(void, incStatementsInsert, (), (override));
  MOCK_METHOD(void, incStatementsOther, (), (override));
  MOCK_METHOD(void, incStatementsSelect, (), (override));
  MOCK_METHOD(void, incStatementsUpdate, (), (override));
  MOCK_METHOD(void, incTransactions, (), (override));
  MOCK_METHOD(void, incTransactionsCommit, (), (override));
  MOCK_METHOD(void, incTransactionsRollback, (), (override));
  MOCK_METHOD(void, incUnknown, (), (override));
  MOCK_METHOD(void, incNotice, (NoticeType), (override));
};

// Define fixture class with decoder and mock callbacks.
class PostgreSQLProxyDecoderTestBase {
public:
  PostgreSQLProxyDecoderTestBase() {
    decoder_ = std::make_unique<DecoderImpl>(&callbacks_);
    decoder_->initialize();
    decoder_->setStartup(false);
  }

protected:
  ::testing::NiceMock<DecoderCallbacksMock> callbacks_;
  std::unique_ptr<DecoderImpl> decoder_;

  // fields often used
  Buffer::OwnedImpl data_;
  uint32_t length_;
  char buf_[256];
  std::string payload_;
};

class PostgreSQLProxyDecoderTest : public PostgreSQLProxyDecoderTestBase, public ::testing::Test {};

// Class is used for parameterized tests for frontend messages.
class PostgreSQLProxyFrontendDecoderTest : public PostgreSQLProxyDecoderTestBase,
                                           public ::testing::TestWithParam<std::string> {};

// Class is used for parameterized tests for encrypted messages.
class PostgreSQLProxyFrontendEncrDecoderTest : public PostgreSQLProxyDecoderTestBase,
                                               public ::testing::TestWithParam<uint32_t> {};

// Class is used for parameterized tests for backend messages.
class PostgreSQLProxyBackendDecoderTest : public PostgreSQLProxyDecoderTestBase,
                                          public ::testing::TestWithParam<std::string> {};

class PostgreSQLProxyErrorTest
    : public PostgreSQLProxyDecoderTestBase,
      public ::testing::TestWithParam<
          std::tuple<std::string, uint32_t, uint32_t, uint32_t, uint32_t>> {};

class PostgreSQLProxyNoticeTest
    : public PostgreSQLProxyDecoderTestBase,
      public ::testing::TestWithParam<std::tuple<std::string, DecoderCallbacks::NoticeType>> {};

// Test processing the startup message from a client.
// For historical reasons, the first message does not include
// command (first byte). It starts with length. The startup
// message contains the protocol version. After processing the
// startup message the server should start using message format
// with command as 1st byte.
TEST_F(PostgreSQLProxyDecoderTest, StartupMessage) {
  decoder_->setStartup(true);

  // start with length
  length_ = htonl(12);
  data_.add(&length_, sizeof(length_));
  // add 8 bytes of some data
  data_.add(buf_, 8);
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);

  // Now feed normal message with 1bytes as command
  data_.add("P");
  length_ = htonl(6); // 4 bytes of length + 2 bytes of data
  data_.add(&length_, sizeof(length_));
  data_.add("AB");
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

// Test processing messages which map 1:1 with buffer.
// The buffer contains just a single entire message and
// nothing more.
TEST_F(PostgreSQLProxyDecoderTest, ReadingBufferSingleMessages) {

  // Feed empty buffer - should not crash
  decoder_->onData(data_, true);

  // Put one byte. This is not enough to parse the message and that byte
  // should stay in the buffer.
  data_.add("P");
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 1);

  // Add length of 4 bytes. It would mean completely empty message.
  // but it should be consumed.
  length_ = htonl(4);
  data_.add(&length_, sizeof(length_));
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);

  // Create a message with 5 additional bytes.
  data_.add("P");
  length_ = htonl(9); // 4 bytes of length field + 5 of data
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 5);
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

// Test simulates situation when decoder is called with incomplete message.
// The message should not be processed until the buffer is filled
// with missing bytes.
TEST_F(PostgreSQLProxyDecoderTest, ReadingBufferLargeMessages) {
  // fill the buffer with message of 100 bytes long
  // but the buffer contains only 98 bytes.
  // It should not be processed.
  data_.add("P");
  length_ = htonl(100); // This also includes length field
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 94);
  decoder_->onData(data_, true);
  // The buffer contains command (1 byte), length (4 bytes) and 94 bytes of message
  ASSERT_THAT(data_.length(), 99);

  // Add 2 missing bytes and feed again to decoder
  data_.add("AB");
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

// Test simulates situation when a buffer contains more than one
// message. Call to the decoder should consume only one message
// at a time and only when the buffer contains the entire message.
TEST_F(PostgreSQLProxyDecoderTest, TwoMessagesInOneBuffer) {
  // create the first message of 50 bytes long (+1 for command)
  data_.add("P");
  length_ = htonl(50);
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 46);

  // create the second message of 50 + 46 bytes (+1 for command)
  data_.add("P");
  length_ = htonl(96);
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 46);
  data_.add(buf_, 46);

  // The buffer contains two messaged:
  // 1st: command (1 byte), length (4 bytes), 46 bytes of data
  // 2nd: command (1 byte), length (4 bytes), 92 bytes of data
  ASSERT_THAT(data_.length(), 148);
  // Process the first message
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 97);
  // Process the second message
  decoder_->onData(data_, true);
  ASSERT_THAT(data_.length(), 0);
}

TEST_F(PostgreSQLProxyDecoderTest, Unknown) {
  // Create invalid message. The first byte is invalid "="
  // Message must be at least 5 bytes to be parsed.
  EXPECT_CALL(callbacks_, incUnknown()).Times(1);
  data_.add("=");
  length_ = htonl(50);
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 46);
  decoder_->onData(data_, true);
}

// Test if each frontend command calls incFrontend method
TEST_P(PostgreSQLProxyFrontendDecoderTest, FrontendInc) {
  EXPECT_CALL(callbacks_, incFrontend()).Times(1);
  data_.add(GetParam());
  length_ = htonl(50);
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 46);
  decoder_->onData(data_, true);
}

// Run the above test for each frontend message
INSTANTIATE_TEST_SUITE_P(FrontEndMessagesTests, PostgreSQLProxyFrontendDecoderTest,
                         ::testing::Values("B", "C", "d", "c", "f", "D", "E", "H", "F", "p", "P",
                                           "p", "Q", "S", "X"));

// Test if X message triggers incRollback and sets proper state in transaction
TEST_F(PostgreSQLProxyFrontendDecoderTest, TerminateMessage) {
  // set decoder state NOT to be in_transaction
  decoder_->getSession().setInTransaction(false);
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(0);
  data_.add("X");
  length_ = htonl(4);
  data_.add(&length_, sizeof(length_));
  decoder_->onData(data_, true);

  // Now set the decoder to be in_transaction state.
  decoder_->getSession().setInTransaction(true);
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(1);
  data_.add("X");
  length_ = htonl(4);
  data_.add(&length_, sizeof(length_));
  decoder_->onData(data_, true);
  ASSERT_FALSE(decoder_->getSession().inTransaction());
}

// Test if each backend command calls incBackend method
TEST_P(PostgreSQLProxyBackendDecoderTest, BackendInc) {
  EXPECT_CALL(callbacks_, incBackend()).Times(1);
  data_.add(GetParam());
  length_ = htonl(50);
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 46);
  decoder_->onData(data_, false);
}

// Run the above test for each backend message
INSTANTIATE_TEST_SUITE_P(BackendMessagesTests, PostgreSQLProxyBackendDecoderTest,
                         ::testing::Values("R", "K", "2", "3", "C", "d", "c", "G", "H", "D", "I",
                                           "E", "V", "v", "n", "N", "A", "t", "S", "1", "s", "Z",
                                           "T"));
// Test parsing backend messages.
// The parser should react only to the first word until the space.
TEST_F(PostgreSQLProxyBackendDecoderTest, ParseStatement) {
  // Payload contains a space after the keyword
  // Rollback counter should be bumped up
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  payload_ = "ROLLBACK 123";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Now try just keyword without a space at the end
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  payload_ = "ROLLBACK";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Partial message should be ignored
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(0);
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload_ = "ROLL";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Keyword without a space  should be ignored
  EXPECT_CALL(callbacks_, incTransactionsRollback()).Times(0);
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload_ = "ROLLBACK123";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());
}

// Test Backend messages and make sure that they
// trigger proper stats updates.
TEST_F(PostgreSQLProxyDecoderTest, Backend) {
  // C message
  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload_ = "BEGIN 123";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());
  ASSERT_TRUE(decoder_->getSession().inTransaction());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload_ = "START TR";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload_ = "COMMIT";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  payload_ = "ROLLBACK";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsInsert());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload_ = "INSERT 1";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsUpdate());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload_ = "UPDATE 1i23";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsDelete());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload_ = "DELETE 88";
  data_.add("C");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());
}

// Test checks deep inspection of the R message
// During login/authentication phase client and server exchange
// multiple R messages. Only payload with length is 8 and
// payload with uint32 number equal to 0 indicates
// successful authentication.
TEST_F(PostgreSQLProxyBackendDecoderTest, AuthenticationMsg) {
  // Create authentication message which does not
  // mean that authentication was OK. The number of
  // sessions must not be increased.
  EXPECT_CALL(callbacks_, incSessionsUnencrypted()).Times(0);
  payload_ = "blah blah";
  data_.add("R");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
  data_.drain(data_.length());

  // Create the correct payload which means that
  // authentication completed successfully.
  EXPECT_CALL(callbacks_, incSessionsUnencrypted());
  data_.add("R");
  length_ = htonl(8);
  data_.add(&length_, sizeof(length_));
  uint32_t code = 0;
  data_.add(&code, sizeof(code));
  decoder_->onData(data_, false);
  data_.drain(data_.length());
}

// Test check parsing of E message. The message
// indicates error.
TEST_P(PostgreSQLProxyErrorTest, ParseErrorMsgs) {
  EXPECT_CALL(callbacks_, incErrorsError()).Times(std::get<1>(GetParam()));
  EXPECT_CALL(callbacks_, incErrorsFatal()).Times(std::get<2>(GetParam()));
  EXPECT_CALL(callbacks_, incErrorsPanic()).Times(std::get<3>(GetParam()));
  EXPECT_CALL(callbacks_, incErrorsUnknown()).Times(std::get<4>(GetParam()));
  payload_ = std::get<0>(GetParam());
  data_.add("E");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
}

INSTANTIATE_TEST_SUITE_P(
    PostgreSQLProxyErrorTestSuite, PostgreSQLProxyErrorTest,
    ::testing::Values(
        std::make_tuple("blah blah", 0, 0, 0, 1), std::make_tuple("SBLAHC1234", 0, 0, 0, 1),
        std::make_tuple("SERRORC1234", 1, 0, 0, 0),
        std::make_tuple("SERRORVERRORC1234", 1, 0, 0, 0),
        std::make_tuple("SFATALVFATALC22012", 0, 1, 0, 0),
        std::make_tuple("SPANICVPANICC22012", 0, 0, 1, 0),
        std::make_tuple("SPANIKVPANICC42501Mkonnte Datei »pg_wal/000000010000000100000096« nicht "
                        "öffnen: Permission deniedFxlog.cL3229RXLogFileInit",
                        0, 0, 1, 0)));

// Test parsing N message. It indicate notice
// and carries additional information about the
// purpose of the message.
TEST_P(PostgreSQLProxyNoticeTest, ParseNoticeMsgs) {
  EXPECT_CALL(callbacks_, incNotice(std::get<1>(GetParam())));
  payload_ = std::get<0>(GetParam());
  data_.add("N");
  length_ = htonl(4 + payload_.length());
  data_.add(&length_, sizeof(length_));
  data_.add(payload_);
  decoder_->onData(data_, false);
}

INSTANTIATE_TEST_SUITE_P(
    PostgreSQLProxyNoticeTestSuite, PostgreSQLProxyNoticeTest,
    ::testing::Values(std::make_tuple("blah blah", DecoderCallbacks::NoticeType::Unknown),
                      std::make_tuple("SblalalaC2345", DecoderCallbacks::NoticeType::Unknown),
                      std::make_tuple("SblahWARNING23345", DecoderCallbacks::NoticeType::Warning),
                      std::make_tuple("SblahblahNOTICEbbal4", DecoderCallbacks::NoticeType::Notice),
                      std::make_tuple("SINFO", DecoderCallbacks::NoticeType::Info),
                      std::make_tuple("SDEBUGDEBUG", DecoderCallbacks::NoticeType::Debug),
                      std::make_tuple("SLLLLOGGGG", DecoderCallbacks::NoticeType::Log)));

// Test checks if the decoder can detect initial message which indicates
// that protocol uses encryption
TEST_P(PostgreSQLProxyFrontendEncrDecoderTest, EncyptedTraffic) {
  // set decoder to wait for initial message
  decoder_->setStartup(true);

  // Initial state is no-encryption
  ASSERT_FALSE(decoder_->encrypted());

  // Create SSLRequest
  EXPECT_CALL(callbacks_, incSessionsEncrypted());
  length_ = htonl(8);
  data_.add(&length_, sizeof(length_));
  // 1234 in the most significant 16 bits, and some code in the least significant 16 bits
  uint32_t code = htonl(GetParam());
  data_.add(&code, sizeof(code));
  decoder_->onData(data_, false);
  ASSERT_TRUE(decoder_->encrypted());
  // Decoder should drain data
  ASSERT_THAT(data_.length(), 0);

  // Now when decoder detected encrypted traffic is should not
  // react to any messages (even not encrypted ones)
  EXPECT_CALL(callbacks_, incFrontend()).Times(0);
  data_.add("P");
  length_ = htonl(50);
  data_.add(&length_, sizeof(length_));
  data_.add(buf_, 46);
  decoder_->onData(data_, true);
  // Decoder should drain data
  ASSERT_THAT(data_.length(), 0);
}

// Run encryption tests.
// 80877103 is SSL code
// 80877104 is GSS code
INSTANTIATE_TEST_SUITE_P(FrontendEncryptedMessagesTests, PostgreSQLProxyFrontendEncrDecoderTest,
                         ::testing::Values(80877103, 80877104));

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
