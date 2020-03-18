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

// Test the Decoder::MessageImpl
#if 0
TEST(PostgreSQLMessageTest, Basic) {
  MessageImpl msg("TestMsg", "Frontend");

  ASSERT_THAT(msg.getDescr(), "TestMsg");
  ASSERT_THAT(msg.getType(), "Frontend");

  // create a function and add it to the message
  MessageImpl::MsgAction action1 =  [](DecoderImpl*){};
  msg.addAction(action1);
  std::reference_wrapper<const std::vector<MessageImpl::MsgAction>> action_list = msg.getActions();
  ASSERT_THAT(action1.target<MessageImpl::MsgAction>(), action_list.get().front().target<MessageImpl::MsgAction>());

  // Add another action;
  MessageImpl::MsgAction action2 = [](DecoderImpl*){};
  msg.addAction(action2);
  action_list = msg.getActions();
  ASSERT_THAT(action_list.get().size(), 2);
  ASSERT_THAT(action1.target<MessageImpl::MsgAction>(), action_list.get().front().target<MessageImpl::MsgAction>());
  ASSERT_THAT(action2.target<MessageImpl::MsgAction>(), action_list.get().front().target<MessageImpl::MsgAction>());
}
#endif
// Define fixture class witrh decoder and mock callbacks.
class PostgreSQLProxyDecoderTest : public ::testing::TestWithParam<std::string> {
public:
  PostgreSQLProxyDecoderTest() {
    decoder_ = std::make_unique<DecoderImpl>(&callbacks_);
    decoder_->initialize();
    decoder_->setInitial(false);
  }
protected:
  ::testing::NiceMock<DecoderCallbacksMock> callbacks_;
  std::unique_ptr<DecoderImpl> decoder_;

  // fields offen used
  Buffer::OwnedImpl data;
  uint32_t length_;
  char buf_[256];
};

// Test processing the initial message from a client.
// For historical reasons, the first message does not include
// command (ats byte). It starts with length. The initial 
// message contains the protocol version. After processing the 
// initial message the server should start using message format
// with command as 1st byte.
TEST_F(PostgreSQLProxyDecoderTest, InitialMessage) {
  decoder_->setInitial(true);

  // start with length
  length_ = htonl(12);
  data.add(&length_, sizeof(length_));
  // add 8 bytes of some data
  data.add(buf_, 8);
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 0);

  // Now feed normal message with 1bytes as command
  data.add("P");
  length_ = htonl(6); // 4 bytes of length + 2 bytes of data
  data.add(&length_, sizeof(length_));
  data.add("AB");
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 0);
}

// Test processing messages which map 1:1 with buffer.
// The buffer contains just a single entire message and
// nothing more.
TEST_F(PostgreSQLProxyDecoderTest, ReadingBufferSingleMessages) {

  // Feed empty buffer - should not crash
  decoder_->onData(data, true);

  // Put one byte. This is not enough to parse the message and that byte
  // should stay in the buffer.
  data.add("P");
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 1);

  // Add length of 4 bytes. It would mean completely empty message.
  // but it should be consumed.
  length_ = htonl(4);
  data.add(&length_, sizeof(length_));
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 0);

  // Create a message with 5 additional bytes.
  data.add("P");
  length_ = htonl(9); // 4 bytes of length field + 5 of data
  data.add(&length_, sizeof(length_));
  data.add(buf_, 5);
  decoder_->onData(data, true);
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
  length_ = htonl(100); // This also includes length field
  data.add(&length_, sizeof(length_));
  data.add(buf_, 94);
  decoder_->onData(data, true);
  // The buffer contains command (1 byte), length (4 bytes) and 94 bytes of message
  ASSERT_THAT(data.length(), 99);

  // Add 2 missing bytes and feed again to decoder 
  data.add("AB");
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 0);
}

// Test simulates situation when a buffer contains more than one
// message. Call to the decoder should consume only one message
// at a time and only when the buffer contains the entire message.
TEST_F(PostgreSQLProxyDecoderTest, TwoMessagesInOneBuffer) {
  // create the first message of 50 bytes long (+1 for command)
  data.add("P");
  length_ = htonl(50);
  data.add(&length_, sizeof(length_));
  data.add(buf_, 46);

  // create the second message of 50 + 46 bytes (+1 for command)
  data.add("P");
  length_ = htonl(96);
  data.add(&length_, sizeof(length_));
  data.add(buf_, 46);
  data.add(buf_, 46);

  // The buffer contains two messaged:
  // 1st: command (1 byte), length (4 bytes), 46 bytes of data
  // 2nd: command (1 byte), length (4 bytes), 92 bytes of data
  ASSERT_THAT(data.length(), 148);
  // Process the first message
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 97);
  // Process the second message
  decoder_->onData(data, true);
  ASSERT_THAT(data.length(), 0);
}

TEST_F(PostgreSQLProxyDecoderTest, Unrecognized)
{
  // Create invalid message. The first byte is invalid "="
  // Message must be at least 5 bytes to be parsed.
  EXPECT_CALL(callbacks_, incUnrecognized())
		.Times(1);
	data.add("=");
  length_ = htonl(50);
  data.add(&length_, sizeof(length_));
  data.add(buf_, 46);
	decoder_->onData(data, true);
}

TEST_P(PostgreSQLProxyDecoderTest, FrontEnd) {
	EXPECT_CALL(callbacks_, incFrontend())
		.Times(1);
	data.add(GetParam());
  length_ = htonl(50);
  data.add(&length_, sizeof(length_));
  data.add(buf_, 46);
	decoder_->onData(data, true);
}


INSTANTIATE_TEST_CASE_P(
  FrontEndMessagesTests,
  PostgreSQLProxyDecoderTest,
  ::testing::Values("P", "Q", "B")
  );

// Test Backend messages
TEST_F(PostgreSQLProxyDecoderTest, Backend) {
  std::string payload;
  // C message
  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload = "BEGIN 123";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload = "START TR";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload = "COMMIT";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  EXPECT_CALL(callbacks_, incTransactionsRollback());
  payload = "ROLLBACK";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsInsert());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload = "INSERT 1";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsUpdate());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload = "UPDATE 1i23";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsDelete());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload = "DELETE 88";
  data.add("C");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  // T message
  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsSelect());
  EXPECT_CALL(callbacks_, incTransactionsCommit());
  payload = "blah blah";
  data.add("T");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  // '1' Message
  EXPECT_CALL(callbacks_, incStatements());
  EXPECT_CALL(callbacks_, incStatementsOther());
  payload = "blah blah";
  data.add("1");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  // E message
  EXPECT_CALL(callbacks_, incErrors());
  payload = "blah VERROR blah";
  data.add("E");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  // N message
  EXPECT_CALL(callbacks_, incWarnings());
  payload = "blah VWARNING blah";
  data.add("N");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());
}

// Test checks deep inspection of the R message
// During login/authentiation phase client and server exchange
// multiple R messages. Only payload with length is 8 and
// payload with uint32 number equal to 0 indicates
// successful authentication.
TEST_F(PostgreSQLProxyDecoderTest, AuthenticationMsg) {
  std::string payload;

  // Create authentication message which does not 
  // mean that authentication was OK. The number of 
  // sessions must not be increased. 
  EXPECT_CALL(callbacks_, incSessions())
    .Times(0);
  payload = "blah blah";
  data.add("R");
  length_ = htonl(4 + payload.length());
  data.add(&length_, sizeof(length_));
  data.add(payload);
  decoder_->onData(data, false);
  data.drain(data.length());

  // Create the correct payload which means that
  // authentication completed successfully.
  EXPECT_CALL(callbacks_, incSessions());
  data.add("R");
  length_ = htonl(8);
  data.add(&length_, sizeof(length_));
  uint32_t code = 0;
  data.add(&code, sizeof(code));
  decoder_->onData(data, false);
  data.drain(data.length());
}

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy {
