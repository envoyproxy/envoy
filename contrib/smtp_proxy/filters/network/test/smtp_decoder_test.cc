#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"

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
    // session_ = std::make_unique<SmtpSession>();
    decoder_impl_ = std::make_unique<DecoderImpl>(&callbacks_);
  }

protected:
  std::unique_ptr<Buffer::OwnedImpl> data_;
  ::testing::NiceMock<MockDecoderCallbacks> callbacks_;
  // std::unique_ptr<SmtpSession> session_;
  std::unique_ptr<DecoderImpl> decoder_impl_;
};

TEST_F(DecoderImplTest, MessageSizeInsufficient) {
  // Test case for insufficient message size
  data_->add("HELL", 4);
  // session_->setState(SmtpSession::State::CONNECTION_SUCCESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
}

TEST_F(DecoderImplTest, EhloHeloCommandsTest) {
  // Test case for EHLO/HELO command
  data_->add("EHLO test.com\r\n", 14);
  decoder_impl_->getSession().setState(SmtpSession::State::CONNECTION_SUCCESS);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, decoder_impl_->getSession().getState());

  data_->drain(14);
  data_->add("HELO test.com\r\n", 14);
  EXPECT_EQ(Decoder::Result::ReadyForNext, decoder_impl_->parseCommand(*data_));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, decoder_impl_->getSession().getState());
}

TEST_F(DecoderImplTest, TestParseCommandStartTls) {
  data_->add("STARTTLS\r\n", 10);
  // Test case when session is already encrypted
  decoder_impl_->getSession().setSessionEncrypted(true);
  decoder_impl_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::outOfOrderCommandResponse));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);

  // Test case when session is not encrypted, and client sends STARTTLS, encryption is successful
  std::cout << "data: " << data_->toString() << "\n";
  decoder_impl_->getSession().setSessionEncrypted(false);
  EXPECT_CALL(callbacks_, upstreamTlsRequired()).WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(false));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);
  EXPECT_EQ(decoder_impl_->getSession().isSessionEncrypted(), true);

  // Test case when callback returns false for downstreamStartTls, encryption error
  decoder_impl_->getSession().setSessionEncrypted(false);
  EXPECT_CALL(callbacks_, upstreamTlsRequired()).WillOnce(testing::Return(false));
  EXPECT_CALL(callbacks_, downstreamStartTls(SmtpUtils::readyToStartTlsResponse))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(callbacks_, incTlsTerminationErrors());
  //   EXPECT_CALL(callbacks_, sendReplyDownstream("454 4.7.0 TLS not available due to temporary
  //   reason"));
  EXPECT_CALL(callbacks_, sendReplyDownstream(SmtpUtils::tlsHandshakeErrorResponse));
  EXPECT_EQ(decoder_impl_->parseCommand(*data_), Decoder::Result::Stopped);
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
