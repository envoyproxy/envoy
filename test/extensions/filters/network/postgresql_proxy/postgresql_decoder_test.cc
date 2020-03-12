#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"
#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

class DecoderCallbacksMock : public DecoderCallbacks {
	public:
  MOCK_METHOD(void, incFrontend, (), (override));
  MOCK_METHOD(void, incUnrecognized, (), (override));
  MOCK_METHOD(void, incErrors, (), (override));
  MOCK_METHOD(void, incSessions, (), (override));
  MOCK_METHOD(void, incStatements, (), (override));
  MOCK_METHOD(void, incStatementsDelete, (), (override));
  MOCK_METHOD(void, incStatementsInsert, (), (override));
  MOCK_METHOD(void, incStatementsOther, (), (override));
  MOCK_METHOD(void, incStatementsSelect, (), (override));
  MOCK_METHOD(void, incStatementsUpdate, (), (override));
  MOCK_METHOD(void, incTransactions, (), (override));
  MOCK_METHOD(void, incTransactionsCommit, (), (override));
  MOCK_METHOD(void, incTransactionsRollback, (), (override));
  MOCK_METHOD(void, incWarnings, (), (override));
};

// Define fixture class witrh decoder and mock callbacks.
class PostgreSQLProxyDecoderTest : public ::testing::Test {
public:
  PostgreSQLProxyDecoderTest() {
    decoder_ = std::make_unique<DecoderImpl>(&callbacks_);
    decoder_->setInitial(false);
  }
protected:
  ::testing::NiceMock<DecoderCallbacksMock> callbacks_;
  std::unique_ptr<DecoderImpl> decoder_;
  Buffer::OwnedImpl data;
};

// Test processing messages which map 1:1 with buffer.
// The buffer contains just a single entire message and
// nothing more.
TEST_F(PostgreSQLProxyDecoderTest, ReadingBufferSingleMessages) {

  // Feed empty buffer - should not crash
  decoder_->onData(data);

  // Put one byte. This is not enough to parse the message and that byte
  // should stay in the buffer.
  data.add("P");
  decoder_->onData(data);
  ASSERT_THAT(data.length(), 1);

  // Add length of 4 bytes. It would mean completely empty message.
  // but it should be consumed.
  uint32_t length = htonl(4);
  data.add(&length, sizeof(length));
  decoder_->onData(data);
  ASSERT_THAT(data.length(), 0);

  // Create a message with 5 additional bytes.
  data.add("P");
  length = htonl(9); // 4 bytes of length field + 5 of data
  data.add(&length, sizeof(length));
  std::unique_ptr<char[]> buf = std::make_unique<char[]>(5); 
  data.add(buf.get(), 5);
  decoder_->onData(data);
  ASSERT_THAT(data.length(), 0);
}

// Test simulates situation when decoder is called with incomplete message.
// The message should not be processed until the buffer is filled
// with missing bytes.
TEST_F(PostgreSQLProxyDecoderTest, ReadingBufferLargeMessages) {
  // fill the buffer with message of 100 bytes long
  // but the buffer contains only 98 bytes.
  // It should not be processed.
  data.add("P");
  uint32_t length = htonl(100); // This also includes length field
  data.add(&length, sizeof(length));
  auto buf = std::make_unique<char[]>(94);
  data.add(buf.get(), 94);
  decoder_->onData(data);
  // The buffer contains command (1 byte), length (4 bytes) and 94 bytes of message
  ASSERT_THAT(data.length(), 99);

  // Add 2 missing bytes and feed again to decoder 
  data.add("AB");
  decoder_->onData(data);
  ASSERT_THAT(data.length(), 0);
}

// Test simulates situation when a buffer contains more than one
// message. Call to the decoder should consume only one message
// at a time and only when the buffer contains the entire message.
TEST_F(PostgreSQLProxyDecoderTest, TwoMessagesInOneBuffer) {
  uint32_t length;

  // create the first message of 50 bytes long (+1 for command)
  data.add("P");
  length = htonl(50);
  data.add(&length, sizeof(length));
  auto buf = std::make_unique<char[]>(46);
  data.add(buf.get(), 46);

  // create the second message of 50 + 46 bytes (+1 for command)
  data.add("P");
  length = htonl(96);
  data.add(&length, sizeof(length));
  data.add(buf.get(), 46);
  data.add(buf.get(), 46);

  // The buffer contains two messaged:
  // 1st: command (1 byte), length (4 bytes), 46 bytes of data
  // 2nd: command (1 byte), length (4 bytes), 92 bytes of data
  ASSERT_THAT(data.length(), 148);
  // Process the first message
  decoder_->onData(data);
  ASSERT_THAT(data.length(), 97);
  // Process the second message
  decoder_->onData(data);
  ASSERT_THAT(data.length(), 0);
}

TEST_F(PostgreSQLProxyDecoderTest, DISABLED_FrontendBasic)
{
	// Create postgresql payload and feed it to decoder.
	EXPECT_CALL(callbacks_, incUnrecognized())
		.Times(1);
	data.add("lalalalal");
	decoder_->onData(data);
	data.drain(data.length());

	EXPECT_CALL(callbacks_, incFrontend())
		.Times(1);
	data.add("P blah");
	decoder_->onData(data);
	data.drain(data.length());

	EXPECT_CALL(callbacks_, incFrontend())
		.Times(1);
	data.add("Q blah");
	decoder_->onData(data);
	data.drain(data.length());

	EXPECT_CALL(callbacks_, incFrontend())
		.Times(1);
	data.add("B blah");
	decoder_->onData(data);
	data.drain(data.length());
}

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy {
