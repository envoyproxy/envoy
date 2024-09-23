#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_session.h"
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

class SmtpSessionTest : public ::testing::Test {
public:
  void SetUp() override {
    data_ = std::make_unique<Buffer::OwnedImpl>();
    session_ = std::make_unique<SmtpSession>(&callbacks_, time_source_, random_);
  }

protected:
  std::unique_ptr<Buffer::OwnedImpl> data_;
  NiceMock<MockDecoderCallbacks> callbacks_;
  std::unique_ptr<SmtpSession> session_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockTimeSystem> time_source_;
  NiceMock<Network::MockConnection> mock_connection_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  NiceMock<MockBuffer> buffer_;
};

TEST_F(SmtpSessionTest, TestNewCommand) {
  // When session is terminated
  std::string cmd = "EHLO";
  SmtpCommand::Type type = SmtpCommand::Type::TransactionCommand;

  session_->newCommand(cmd, type);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), cmd);
  EXPECT_EQ(session_->getCurrentCommand()->getType(), type);
}

TEST_F(SmtpSessionTest, TestHandleCommand) {
  std::string cmd = "";
  std::string args = "";

  EXPECT_EQ(session_->handleCommand(cmd, args), SmtpUtils::Result::ReadyForNext);
  EXPECT_FALSE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand(), nullptr);

  cmd = "EHLO";
  EXPECT_EQ(session_->handleCommand(cmd, args), SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), cmd);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInitRequest);
}

TEST_F(SmtpSessionTest, TestHandleMail) {

  // received MAIL from cmd when session not in progress
  std::string arg = "FROM:<test@test.com>";
  session_->setState(SmtpSession::State::ConnectionSuccess);

  // ON_CALL(callbacks_, sendReplyDownstream(_)).WillByDefault(testing::Return(false));
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Please introduce yourself first")));

  auto result = session_->handleMail(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::ConnectionSuccess);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // invalid FROM arg syntax
  arg = "test@test.com";
  session_->setState(SmtpSession::State::SessionInProgress);
  // ON_CALL(callbacks_, sendReplyDownstream(_)).WillByDefault(testing::Return(false));
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 2}, "Bad MAIL arg syntax of FROM:<address>")));

  result = session_->handleMail(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  arg = "FRO:";
  session_->setState(SmtpSession::State::SessionInProgress);
  // ON_CALL(callbacks_, sendReplyDownstream(_)).WillByDefault(testing::Return(false));
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 2}, "Bad MAIL arg syntax of FROM:<address>")));

  result = session_->handleMail(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);
}

TEST_F(SmtpSessionTest, TestHandleMail_ValidAddr) {
  // Valid FROM address
  std::string arg = "FROM:<test@test.com>";
  std::string sender = "test@test.com";
  session_->setState(SmtpSession::State::SessionInProgress);
  auto result = session_->handleMail(arg);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::smtpMailCommand);
  EXPECT_EQ(session_->getTransaction()->getSender(), sender);
  EXPECT_EQ(session_->getTransactionState(), SmtpTransaction::State::TransactionRequest);
}

TEST_F(SmtpSessionTest, TestHandleRcpt) {

  // received RCPT TO cmd when transaction is not in progress
  std::string arg = "TO:<test@test.com>";
  session_->setState(SmtpSession::State::SessionInProgress);

  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Missing MAIL FROM command")));

  auto result = session_->handleRcpt(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  EXPECT_EQ(session_->getTransaction(), nullptr);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // invalid RCPT TO arg syntax
  arg = "test@test.com";
  session_->createNewTransaction();
  session_->setState(SmtpSession::State::SessionInProgress);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 2}, "Bad RCPT arg syntax of TO:<address>")));

  result = session_->handleRcpt(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  arg = "TO:";
  session_->setState(SmtpSession::State::SessionInProgress);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 2}, "Bad RCPT arg syntax of TO:<address>")));

  result = session_->handleRcpt(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Valid RCPT TO address
  arg = "TO:<test@test.com>";
  session_->setState(SmtpSession::State::SessionInProgress);
  result = session_->handleRcpt(arg);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::smtpRcptCommand);
  EXPECT_EQ(session_->getTransaction()->getNoOfRecipients(), 1);
  EXPECT_EQ(session_->getTransactionState(), SmtpTransaction::State::RcptCommand);
}

TEST_F(SmtpSessionTest, TestHandleData) {

  // received DATA cmd with arguments
  std::string arg = "arg1234";
  session_->setState(SmtpSession::State::SessionInProgress);

  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 4}, "No params allowed for DATA command")));

  auto result = session_->handleData(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // received DATA cmd when no transaction in progress
  arg = "";
  ASSERT_EQ(session_->getTransaction(), nullptr);
  session_->setState(SmtpSession::State::SessionInProgress);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Missing RCPT TO command")));

  result = session_->handleData(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  ASSERT_EQ(session_->getTransaction(), nullptr);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Received DATA command when no RCTP address is received in a trxn
  session_->setState(SmtpSession::State::SessionInProgress);
  session_->createNewTransaction();
  arg = "";
  ASSERT_EQ(session_->getTransaction()->getNoOfRecipients(), 0);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Missing RCPT TO command")));

  result = session_->handleData(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Add rcpt address to trxn.
  std::string rcpt = "test@test.com";
  session_->getTransaction()->addRcpt(rcpt);

  arg = "";
  result = session_->handleData(arg);
  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_TRUE(session_->isDataTransferInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::smtpDataCommand);
  EXPECT_EQ(session_->getTransactionState(), SmtpTransaction::State::MailDataTransferRequest);
}

TEST_F(SmtpSessionTest, TestHandleAuth) {

  // received AUTH command when session is not in progress.
  session_->setState(SmtpSession::State::ConnectionSuccess);

  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Please introduce yourself first")));

  auto result = session_->handleAuth();

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::ConnectionSuccess);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // received AUTH cmd when session in progress - valid sequence.
  session_->setState(SmtpSession::State::SessionInProgress);
  result = session_->handleAuth();

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::smtpAuthCommand);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionAuthRequest);

  // Received AUTH cmd when session is already authenticated.
  session_->setAuthStatus(true);
  session_->setState(SmtpSession::State::SessionInProgress);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Already authenticated")));

  result = session_->handleAuth();

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
}

TEST_F(SmtpSessionTest, TestHandleStarttls) {

  // received STARTTLS command and upstream TLS is enabled in config.
  EXPECT_CALL(callbacks_, upstreamTlsEnabled()).WillOnce(Return(true));

  auto result = session_->handleStarttls();

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::startTlsCommand);
  EXPECT_EQ(session_->getState(), SmtpSession::State::UpstreamTlsNegotiation);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // received STARTTLS command and upstream TLS is disabled in config i.e. only downstream tls
  // termination required.
  session_->setState(SmtpSession::State::SessionInProgress);
  EXPECT_CALL(callbacks_, upstreamTlsEnabled()).WillOnce(Return(false));

  result = session_->handleStarttls();

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::startTlsCommand);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);

  // Received STARTTLS cmd when session is already encrypted.
  session_->setSessionEncrypted(true);
  session_->setState(SmtpSession::State::SessionInProgress);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              502, {5, 5, 1}, "Already running in TLS")));

  result = session_->handleStarttls();

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
}

TEST_F(SmtpSessionTest, TestHandleReset) {

  // received RSET command with arguments.
  std::string arg = "arg1234";
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 4}, "No params allowed for RSET command")));

  auto result = session_->handleReset(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // RSET command is accepted.
  arg = "";
  result = session_->handleReset(arg);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::smtpRsetCommand);

  // Received RSET when transaction is in progress.
  session_->setState(SmtpSession::State::SessionInProgress);
  session_->createNewTransaction();

  arg = "";
  result = session_->handleReset(arg);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_EQ(session_->getTransactionState(), SmtpTransaction::State::TransactionAbortRequest);
}

TEST_F(SmtpSessionTest, TestHandleQuit) {

  // received QUIT command with arguments.
  std::string arg = "arg1234";
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::generateResponse(
                              501, {5, 5, 4}, "No params allowed for QUIT command")));

  auto result = session_->handleQuit(arg);

  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // QUIT command is accepted.
  arg = "";
  result = session_->handleQuit(arg);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::smtpQuitCommand);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionTerminationRequest);

  // Received QUIT when transaction is in progress.
  session_->setState(SmtpSession::State::SessionInProgress);
  session_->createNewTransaction();
  arg = "";
  result = session_->handleQuit(arg);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionTerminationRequest);
  EXPECT_EQ(session_->getTransactionState(), SmtpTransaction::State::TransactionAbortRequest);
}

TEST_F(SmtpSessionTest, TestHandleOtherCmds) {
  std::string cmd = SmtpUtils::xReqIdCommand;

  auto result = session_->handleOtherCmds(cmd);
  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_TRUE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::xReqIdCommand);
}

TEST_F(SmtpSessionTest, TestHandleConnResponse) {
  uint16_t response_code = 220;
  std::string response = "220 localhost ESMTP Service Ready";

  // Test Connection Success, tracing is not enabled.
  // ASSERT_FALSE(callbacks_.tracingEnabled());
  ON_CALL(callbacks_, tracingEnabled()).WillByDefault(Return(false));
  // EXPECT_CALL(callbacks_, tracingEnabled()).WillOnce(Return(false));

  auto result = session_->handleConnResponse(response_code, response);
  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_EQ(session_->getState(), SmtpSession::State::ConnectionSuccess);

  // Test Tracing Enabled

  EXPECT_CALL(callbacks_, tracingEnabled()).WillOnce(Return(true));
  EXPECT_CALL(callbacks_, sendUpstream(_)).WillOnce(Return(true));

  result = session_->handleConnResponse(response_code, response);
  EXPECT_EQ(result, SmtpUtils::Result::Stopped);
  EXPECT_EQ(session_->getState(), SmtpSession::State::XReqIdTransfer);
  EXPECT_EQ(session_->getResponseOnHold(), response + SmtpUtils::smtpCrlfSuffix);
  EXPECT_EQ(session_->getCurrentCommand()->getName(), SmtpUtils::xReqIdCommand);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // Test 5xx Error Response
  response_code = 554;
  EXPECT_CALL(callbacks_, incSmtpConnectionEstablishmentErrors());
  result = session_->handleConnResponse(response_code, response);
  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
}

TEST_F(SmtpSessionTest, TestHandleEhloResponse) {
  uint16_t response_code = 250;
  std::string response = "250-smtp.example.com\r\n250-PIPELINING\r\n250-SIZE 10240000\r\n";
  session_->newCommand(SmtpUtils::smtpEhloCommand, SmtpCommand::Type::NonTransactionCommand);

  SmtpUtils::Result result = session_->handleEhloResponse(response_code, response);

  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  EXPECT_FALSE(session_->isCommandInProgress());
  EXPECT_EQ(session_->getCurrentCommand()->getResponseCode(), response_code);

  // Test case where response code is not 250.
  response_code = 500;
  response = "500 Internal Server Error\r\n";
  result = session_->handleEhloResponse(response_code, response);
  EXPECT_EQ(result, SmtpUtils::Result::ReadyForNext);
  EXPECT_EQ(session_->getState(), SmtpSession::State::ConnectionSuccess);
  EXPECT_EQ(session_->getCurrentCommand()->getResponseCode(), response_code);
}

TEST_F(SmtpSessionTest, TestHandleDownstreamTls) {

  // downstreamStartTls returns false, i.e. downstream tls is successful
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, incTlsTerminatedSessions());
  session_->handleDownstreamTls();
  EXPECT_EQ(session_->isSessionEncrypted(), true);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionInProgress);
  testing::Mock::VerifyAndClearExpectations(&callbacks_);

  // downstreamStartTls returns true, i.e. when downstream tls failed
  session_->setSessionEncrypted(false);
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(callbacks_, incTlsTerminationErrors());
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::tlsHandshakeErrorResponse));
  // EXPECT_CALL(callbacks_, closeDownstreamConnection());
  session_->handleDownstreamTls();
  EXPECT_EQ(session_->isSessionEncrypted(), false);
  EXPECT_EQ(session_->getState(), SmtpSession::State::SessionTerminated);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy