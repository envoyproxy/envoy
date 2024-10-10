#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder_impl.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"
#include "contrib/smtp_proxy/filters/network/test/smtp_test_utils.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class MockSmtpSession : public SmtpSession {
public:
  MockSmtpSession(DecoderCallbacks* callbacks, TimeSource& time_source,
                  Random::RandomGenerator& random_generator)
      : SmtpSession(callbacks, time_source, random_generator) {}
  ~MockSmtpSession() {}
  MOCK_METHOD(bool, isTerminated, (), (override));
  MOCK_METHOD(bool, isDataTransferInProgress, (), (override));
  MOCK_METHOD(bool, isCommandInProgress, (), (override));
  // MOCK_METHOD(SmtpSession::State, getState, (), (override));
  MOCK_METHOD(void, updateBytesMeterOnCommand, (Buffer::Instance & data), (override));
  MOCK_METHOD(SmtpUtils::Result, handleCommand, (std::string & command, std::string& args),
              (override));
  MOCK_METHOD(SmtpUtils::Result, handleResponse, (uint16_t & response_code, std::string& response),
              (override));
};

class DecoderImplTest : public ::testing::Test {
public:
  void SetUp() override {
    data_ = std::make_unique<Buffer::OwnedImpl>();
    session_ = std::make_unique<NiceMock<MockSmtpSession>>(&callbacks_, time_source_, random_);
    decoder_ = std::make_unique<DecoderImpl>(&callbacks_, time_source_, random_);
    decoder_->setSession(session_.get());
  }

protected:
  std::unique_ptr<Buffer::OwnedImpl> data_;
  NiceMock<MockDecoderCallbacks> callbacks_;
  std::unique_ptr<NiceMock<MockSmtpSession>> session_;
  std::unique_ptr<DecoderImpl> decoder_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockTimeSystem> time_source_;
};

TEST_F(DecoderImplTest, TestParseCommand) {
  // When session is terminated
  data_->add("EHLO test.com\r\n");
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(true));
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  testing::Mock::VerifyAndClearExpectations(session_.get());

  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(true));
  EXPECT_CALL(*session_, updateBytesMeterOnCommand(Ref(*data_)));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command without CRLF ending
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("EHLO test.com");

  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length < 4 (excluding CRLF)
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("EHL\r\n");

  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length = 4 (excluding CRLF)
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("RSET\r\n");

  // Expected command and args parsed.
  std::string command = "RSET";
  std::string args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length > 4 (excluding CRLF), below cmd will not be processed and it will be
  // forwarded to upstream.
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("RSETY\r\n");

  command = "";
  args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length = 6 (excluding CRLF)
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("RSETYZ\r\n");

  command = "";
  args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length = 4 followed by a space and no args
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("RSET \r\n");

  // Expected command and args parsed.
  command = "";
  args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length = 4 followed by a 2 spaces
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("RSET  \r\n");

  // Expected command and args parsed.
  command = "";
  args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP command with length = 4 followed by a space and arg with 1 char
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("CMMD Y\r\n");

  // Expected command and args parsed.
  command = "CMMD";
  args = "Y";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // A valid SMTP command with args
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("MAIL FROM:<test@example.com>\r\n");

  // Expected command and args parsed.
  command = "MAIL";
  args = "FROM:<test@example.com>";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP starttls command
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("STARTTLS\r\n");

  command = "STARTTLS";
  args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));

  // SMTP starttls command followed by a char/space, will not be parsed
  testing::Mock::VerifyAndClearExpectations(session_.get());
  data_->drain(data_->length());
  data_->add("STARTTLS \r\n");

  command = "";
  args = "";
  EXPECT_CALL(*session_, isTerminated()).WillOnce(Return(false));
  EXPECT_CALL(*session_, isDataTransferInProgress()).WillOnce(Return(false));
  EXPECT_CALL(*session_, handleCommand(command, args))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));

  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseCommand(*data_));
}

TEST_F(DecoderImplTest, TestParseResponse) {

  session_->setState(SmtpSession::State::ConnectionSuccess);

  // No command is currently being processed,so response will not be processed
  EXPECT_CALL(*session_, isCommandInProgress()).WillOnce(Return(false));
  data_->add("220 OK");
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseResponse(*data_));
  testing::Mock::VerifyAndClearExpectations(session_.get());

  // SMTP response without CRLF ending, will not be processed.
  data_->drain(data_->length());
  data_->add("220 OK");
  EXPECT_CALL(*session_, isCommandInProgress()).WillOnce(Return(true));
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseResponse(*data_));
  testing::Mock::VerifyAndClearExpectations(session_.get());

  // SMTP response length < 3 (exlcuding CRLF)
  data_->drain(data_->length());
  data_->add("22\r\n");
  EXPECT_CALL(*session_, isCommandInProgress()).WillOnce(Return(true));
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseResponse(*data_));
  testing::Mock::VerifyAndClearExpectations(session_.get());

  // SMTP response length = 3 (exlcuding CRLF)
  data_->drain(data_->length());
  data_->add("220\r\n");

  // Expected response code and response string
  uint16_t resp_code = 220;
  std::string resp = "220";
  EXPECT_CALL(*session_, isCommandInProgress()).WillOnce(Return(true));
  EXPECT_CALL(*session_, handleResponse(resp_code, resp))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseResponse(*data_));
  testing::Mock::VerifyAndClearExpectations(session_.get());

  // SMTP response received, No command in progress but session state is ConnectionRequest
  data_->drain(data_->length());
  data_->add("220 OK\r\n");
  EXPECT_CALL(*session_, isCommandInProgress()).WillOnce(Return(false));
  session_->setState(SmtpSession::State::ConnectionRequest);

  // Expected response code and response string
  resp_code = 220;
  resp = "220 OK";
  EXPECT_CALL(*session_, handleResponse(resp_code, resp))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseResponse(*data_));
  testing::Mock::VerifyAndClearExpectations(session_.get());

  session_->setState(SmtpSession::State::ConnectionSuccess);

  // SMTP response with invalid response code
  data_->drain(data_->length());
  data_->add("abc OK\r\n");

  // Expected response code and response string
  resp_code = 0;
  resp = "abc OK";
  EXPECT_CALL(*session_, isCommandInProgress()).WillOnce(Return(true));
  EXPECT_CALL(*session_, handleResponse(resp_code, resp))
      .WillOnce(Return(SmtpUtils::Result::ReadyForNext));
  EXPECT_EQ(SmtpUtils::Result::ReadyForNext, decoder_->parseResponse(*data_));
  testing::Mock::VerifyAndClearExpectations(session_.get());
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy