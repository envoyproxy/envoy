#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"
#include "contrib/smtp_proxy/filters/network/test/smtp_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class MockDecoderCallbacks : public DecoderCallbacks {
public:
  MOCK_METHOD(void, incSmtpTransactions, (), (override));
  MOCK_METHOD(void, incSmtpTransactionsAborted, (), (override));
  MOCK_METHOD(void, incSmtpSessionRequests, (), (override));
  MOCK_METHOD(void, incSmtpConnectionEstablishmentErrors, (), (override));
  MOCK_METHOD(void, incSmtpSessionsCompleted, (), (override));
  MOCK_METHOD(void, incSmtpSessionsTerminated, (), (override));
  MOCK_METHOD(void, incTlsTerminatedSessions, (), (override));
  MOCK_METHOD(void, incTlsTerminationErrors, (), (override));
  MOCK_METHOD(void, incUpstreamTlsSuccess, (), (override));
  MOCK_METHOD(void, incUpstreamTlsFailed, (), (override));

  MOCK_METHOD(void, incSmtpAuthErrors, (), (override));
  MOCK_METHOD(void, incMailDataTransferErrors, (), (override));
  MOCK_METHOD(void, incMailRcptErrors, (), (override));

  MOCK_METHOD(bool, downstreamStartTls, (absl::string_view), (override));
  MOCK_METHOD(bool, sendReplyDownstream, (absl::string_view), (override));
  MOCK_METHOD(bool, upstreamTlsRequired, (), (const));
  MOCK_METHOD(bool, upstreamStartTls, (), (override));
  MOCK_METHOD(void, closeDownstreamConnection, (), (override));
};

class DecoderImplTest : public ::testing::Test {
public:
  void SetUp() override {
    data_ = std::make_unique<Buffer::OwnedImpl>();
    decoder_impl_ = std::make_unique<DecoderImpl>(&callbacks_);
  }

protected:
  std::unique_ptr<Buffer::OwnedImpl> data_;
  ::testing::NiceMock<MockDecoderCallbacks> callbacks_;
  std::unique_ptr<DecoderImpl> decoder_impl_;
};

TEST_F(DecoderImplTest, TestConnectionSuccess) {
  // Set the initial state of the session to CONNECTION_REQUEST.
  decoder_impl_->getSession().setState(SmtpSession::State::CONNECTION_REQUEST);

  // Prepare the response data.
  data_->add("220 \r\n");

  // Call the parseResponse function.
  decoder_impl_->parseResponse(*data_);

  // Check that the session state was correctly updated.
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::CONNECTION_SUCCESS);

  decoder_impl_->getSession().setState(SmtpSession::State::CONNECTION_REQUEST);

  data_->drain(data_->length());
  // Prepare the error response data.
  data_->add("554 \r\n");
  EXPECT_CALL(callbacks_, incSmtpConnectionEstablishmentErrors());
  ;
  // Call the parseResponse function.
  decoder_impl_->parseResponse(*data_);
  // Check that the session state was correctly updated.
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::CONNECTION_REQUEST);
}

TEST_F(DecoderImplTest, EhloHeloCommandsTest) {
  // Test case for EHLO/HELO command
  data_->add("EHLO test.com\r\n", 14);
  decoder_impl_->getSession().setState(SmtpSession::State::CONNECTION_SUCCESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, decoder_impl_->getSession().getState());

  data_->drain(data_->length());
  data_->add("HELO test.com\r\n", 14);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, decoder_impl_->getSession().getState());
}

TEST_F(DecoderImplTest, MessageSizeInsufficient) {
  // Test case for insufficient message size
  data_->add("HELL", 4);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
}

TEST_F(DecoderImplTest, TestParseCommand) {
  // When the session state is SESSION_IN_PROGRESS and an invalid command is received, it is passed
  // to upstream, without any state change.
  data_->add("invalid command\r\n");
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_IN_PROGRESS, decoder_impl_->getSession().getState());

  // When the session state is SESSION_IN_PROGRESS and the AUTH command is received
  data_->drain(data_->length());
  data_->add(SmtpTestUtils::smtpAuthCommand);
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_AUTH_REQUEST, decoder_impl_->getSession().getState());

  // When the session state is SESSION_IN_PROGRESS and another HELO?EHLO command is received
  data_->drain(data_->length());
  data_->add(SmtpTestUtils::smtpEhloCommand);
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, decoder_impl_->getSession().getState());

  // When the session state is SESSION_IN_PROGRESS and the QUIT command is received
  data_->drain(data_->length());
  data_->add("quit\r\n");
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_TERMINATION_REQUEST,
            decoder_impl_->getSession().getState());

  // When the session state is SESSION_TERMINATED and any command is received, filter drops the
  // request.
  data_->drain(data_->length());
  data_->add(SmtpTestUtils::smtpMailCommand);
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_TERMINATED);
  EXPECT_EQ(Decoder::Result::Stopped, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_TERMINATED, decoder_impl_->getSession().getState());
}

TEST_F(DecoderImplTest, TestParseCommandStartTls) {

  // param not allowed for starttls command
  data_->add("STARTTLS param\r\n");
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::syntaxErrorNoParamsAllowed));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  data_->drain(data_->length());
  data_->add("STARTTLS\r\n", 10);
  // Test case when session is already encrypted
  decoder_impl_->getSession().setSessionEncrypted(true);
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::outOfOrderCommandResponse));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Test case when session is not encrypted, and client sends STARTTLS, encryption is successful
  decoder_impl_->getSession().setSessionEncrypted(false);
  EXPECT_CALL(callbacks_, upstreamTlsRequired()).WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(false));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);
  EXPECT_EQ(decoder_impl_->getSession().isSessionEncrypted(), true);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Test case when callback returns false for downstreamStartTls, encryption error
  decoder_impl_->getSession().setSessionEncrypted(false);
  EXPECT_CALL(callbacks_, upstreamTlsRequired()).WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(callbacks_, incTlsTerminationErrors());
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::tlsHandshakeErrorResponse));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  decoder_impl_->getSession().setState(SmtpSession::State::UPSTREAM_TLS_NEGOTIATION);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::mailboxUnavailableResponse));
  data_->drain(data_->length());
  data_->add("EHLO\r\n");
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);

  // When upstream TLS is required.
  testing::Mock::VerifyAndClearExpectations(&callbacks_);
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);

  data_->drain(data_->length());
  data_->add("STARTTLS\r\n");

  EXPECT_CALL(callbacks_, upstreamTlsRequired()).WillOnce(testing::Return(true));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::ReadyForNext);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::UPSTREAM_TLS_NEGOTIATION);
}

TEST_F(DecoderImplTest, TestSmtpTransactionCommands) {

  // Test case 1: NONE or TRANSACTION_COMPLETED state and smtpMailCommand
  std::string command = SmtpTestUtils::smtpMailCommand;
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::NONE);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_REQUEST);

  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_COMPLETED);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_REQUEST);

  // Test case 2: TRANSACTION_IN_PROGRESS state and smtpRcptCommand
  command = SmtpTestUtils::smtpRcptCommand;
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::RCPT_COMMAND);

  // Test multiple RCPT commands
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::RCPT_COMMAND);

  // Test case 3: TRANSACTION_IN_PROGRESS state and smtpDataCommand
  command = SmtpUtils::smtpDataCommand;
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);

  // Transaction abort/reset
  command = SmtpUtils::smtpRsetCommand;
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);

  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);

  command = SmtpTestUtils::smtpEhloCommand;
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);

  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
  decoder_impl_->decodeSmtpTransactionCommands(command);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);
}

TEST_F(DecoderImplTest, TestParseResponse) {
  // Set the initial state of the session to SESSION_INIT_REQUEST.
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_INIT_REQUEST);
  // Prepare the response data.
  data_->add("250");
  // Call the parseResponse function.
  decoder_impl_->parseResponse(*data_);
  // Check that the session state was correctly updated.
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_IN_PROGRESS);

  // When EHLO commmand aborts the session when transaction is in progress
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_INIT_REQUEST);
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);

  data_->drain(data_->length());
  data_->add("250");
  EXPECT_CALL(callbacks_, incSmtpTransactionsAborted());
  decoder_impl_->parseResponse(*data_);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(), SmtpTransaction::State::NONE);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_IN_PROGRESS);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Set the initial state of the session to SESSION_AUTH_REQUEST.
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_AUTH_REQUEST);
  // Prepare the error response data.
  data_->drain(data_->length());
  data_->add("500");
  // Check that the "smtp_auth_errors" stat was correctly incremented.
  EXPECT_CALL(callbacks_, incSmtpAuthErrors());
  // Call the parseResponse function.
  decoder_impl_->parseResponse(*data_);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_IN_PROGRESS);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // When AUTH request gets 334 response from server
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_AUTH_REQUEST);
  data_->drain(data_->length());
  data_->add("334");
  EXPECT_CALL(callbacks_, incSmtpAuthErrors()).Times(0);
  decoder_impl_->parseResponse(*data_);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_AUTH_REQUEST);

  // When AUTH request is successful
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_AUTH_REQUEST);
  data_->drain(data_->length());
  data_->add("200");
  decoder_impl_->parseResponse(*data_);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_IN_PROGRESS);

  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Set session state to UPSTREAM_TLS_NEGOTIATION
  decoder_impl_->getSession().setState(SmtpSession::State::UPSTREAM_TLS_NEGOTIATION);
  data_->drain(data_->length());
  data_->add("220");
  // Test the case where the response code is 220
  EXPECT_CALL(callbacks_, upstreamStartTls()).WillOnce(testing::Return(true));
  EXPECT_EQ(Decoder::Result::Stopped, decoder_impl_->parseResponse(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_IN_PROGRESS, decoder_impl_->getSession().getState());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Test the case where the response code is 220 but failed to change upstream socket to TLS
  decoder_impl_->getSession().setState(SmtpSession::State::UPSTREAM_TLS_NEGOTIATION);
  EXPECT_CALL(callbacks_, upstreamStartTls()).WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::tlsNotSupportedResponse));
  EXPECT_CALL(callbacks_, closeDownstreamConnection());
  EXPECT_EQ(Decoder::Result::Stopped, decoder_impl_->parseResponse(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_TERMINATED, decoder_impl_->getSession().getState());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Reset session state to UPSTREAM_TLS_NEGOTIATION
  decoder_impl_->getSession().setState(SmtpSession::State::UPSTREAM_TLS_NEGOTIATION);
  data_->drain(data_->length());
  data_->add("554");

  // Test the case where the response code is 5xx.
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::tlsNotSupportedResponse));
  EXPECT_EQ(Decoder::Result::Stopped, decoder_impl_->parseResponse(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_TERMINATED, decoder_impl_->getSession().getState());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // 221 response (to QUIT command) when session state is SESSION_TERMINATION_REQUEST
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_TERMINATION_REQUEST);
  data_->drain(data_->length());
  data_->add("221");
  EXPECT_CALL(callbacks_, incSmtpSessionsCompleted());
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseResponse(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_TERMINATED, decoder_impl_->getSession().getState());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Quit command received when smtp transaction is in progress and session is terminated.
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_TERMINATION_REQUEST);
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
  data_->drain(data_->length());
  data_->add("221");
  EXPECT_CALL(callbacks_, incSmtpSessionsCompleted());
  EXPECT_CALL(callbacks_, incSmtpTransactionsAborted());
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseResponse(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_TERMINATED, decoder_impl_->getSession().getState());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // If quit command receives non 2xx response from server, session state is set back to
  // in-progress.
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_TERMINATION_REQUEST);
  data_->drain(data_->length());
  data_->add("500");
  EXPECT_CALL(callbacks_, incSmtpSessionsCompleted()).Times(0);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseResponse(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_IN_PROGRESS, decoder_impl_->getSession().getState());
  testing::Mock::VerifyAndClearExpectations(&callbacks_);
}

TEST_F(DecoderImplTest, TestDecodeSmtpTransactionResponse) {

  // Success response to TRANSACTION_REQUEST
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_REQUEST);
  uint16_t response_code = 250;
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_IN_PROGRESS);

  // Error response to TRANSACTION_REQUEST
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::TRANSACTION_REQUEST);
  response_code = 400;
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(), SmtpTransaction::State::NONE);

  // Success response to RCPT COMMAND
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::RCPT_COMMAND);
  response_code = 250;
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_IN_PROGRESS);

  // Error response to RCPT COMMAND
  decoder_impl_->getSession().SetTransactionState(SmtpTransaction::State::RCPT_COMMAND);
  response_code = 500;
  EXPECT_CALL(callbacks_, incMailRcptErrors());
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::RCPT_COMMAND);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // 2xx Success response to DATA Command
  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
  response_code = 250;
  EXPECT_CALL(callbacks_, incSmtpTransactions());
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_COMPLETED);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Error response to DATA Command
  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
  response_code = 500;
  EXPECT_CALL(callbacks_, incMailDataTransferErrors());
  EXPECT_CALL(callbacks_, incSmtpTransactions());
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(), SmtpTransaction::State::NONE);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // 3xx Intermediate response to DATA Command
  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
  response_code = 339;
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Transaction abort request is successful
  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);
  response_code = 250;
  EXPECT_CALL(callbacks_, incSmtpTransactionsAborted());
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(), SmtpTransaction::State::NONE);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Transaction abort request failed
  decoder_impl_->getSession().SetTransactionState(
      SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);
  response_code = 500;
  decoder_impl_->decodeSmtpTransactionResponse(response_code);
  EXPECT_EQ(decoder_impl_->getSession().getTransactionState(),
            SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
}

TEST_F(DecoderImplTest, TestHandleDownstreamTls) {

  // downstreamStartTls returns false, i.e. downstream tls is successful
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(false));
  decoder_impl_->handleDownstreamTls();
  EXPECT_EQ(decoder_impl_->getSession().isSessionEncrypted(), true);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_IN_PROGRESS);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // downstreamStartTls returns true, i.e. when downstream tls failed
  decoder_impl_->getSession().setSessionEncrypted(false);
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(callbacks_, incTlsTerminationErrors());
  EXPECT_CALL(callbacks_, closeDownstreamConnection());
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::tlsHandshakeErrorResponse));
  decoder_impl_->handleDownstreamTls();
  EXPECT_EQ(decoder_impl_->getSession().isSessionEncrypted(), false);
  EXPECT_EQ(decoder_impl_->getSession().getState(), SmtpSession::State::SESSION_TERMINATED);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
