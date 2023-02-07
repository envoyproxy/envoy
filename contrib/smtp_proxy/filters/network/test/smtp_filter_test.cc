#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpFilterTest : public testing::Test {
public:
  SmtpFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<SmtpFilterConfig>(config_options_, scope_);
    filter_ = std::make_unique<SmtpFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  SmtpFilterConfigSharedPtr config_;
  SmtpFilterConfig::SmtpFilterConfigOptions config_options_{
      stat_prefix_,
      envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy_UpstreamTLSMode_DISABLE};

  std::unique_ptr<SmtpFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test."};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  Buffer::OwnedImpl data_;
};

// Test New Session counter increment
TEST_F(SmtpFilterTest, NewSessionStatsTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(filter_->getSession().getState(), SmtpSession::State::CONNECTION_REQUEST);

  EXPECT_EQ(1, config_->stats().smtp_session_requests_.value());
}

TEST_F(SmtpFilterTest, TestDownstreamStarttls) {
  initialize();

  //Upstream TLS is disabled, testing only downstream starttls handling
  filter_->getConfig()->upstream_tls_ =
      envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE;
  ASSERT_FALSE(filter_->upstreamTlsRequired());
  filter_->getSession().setState(SmtpSession::State::CONNECTION_SUCCESS);
  
  data_.add("EHLO localhost\r\n");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, filter_->getSession().getState());

  data_.drain(data_.length());

  data_.add("250-Hello localhost\r\n250-PIPELINING\r\n250-8BITMIME\r\n250-STARTTLS\r\n");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onWrite(data_, false));
  EXPECT_EQ(SmtpSession::State::SESSION_IN_PROGRESS, filter_->getSession().getState());
  data_.drain(data_.length());
  data_.add("STARTTLS\r\n");
  
  //Downstream TLS termination successful after STARTTLS

  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
  Network::Connection::BytesSentCb cb;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));
  Buffer::OwnedImpl buf;
  EXPECT_CALL(connection_, write(_, false)).WillOnce(testing::SaveArg<0>(&buf));
  
  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  ASSERT_STREQ(SmtpUtils::readyToStartTlsResponse, buf.toString().c_str());

  // Now indicate through the callback that 220 response has been sent.
  // Filter should call startSecureTransport and should not close the connection.
  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(testing::Return(true));
  EXPECT_CALL(connection_, close(_)).Times(0);
  cb(buf.length());

  EXPECT_EQ(SmtpSession::State::SESSION_IN_PROGRESS, filter_->getSession().getState());
  EXPECT_EQ(config_->stats().smtp_tls_terminated_sessions_.value(), 1);

  //Send starttls command again, receive 503 out of order command response from filter.
  buf.drain(buf.length());
  data_.add("STARTTLS\r\n");
  
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));
  EXPECT_CALL(connection_, write(_, false)).WillOnce(testing::SaveArg<0>(&buf));

  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  ASSERT_STREQ(SmtpUtils::outOfOrderCommandResponse, buf.toString().c_str());

}

TEST_F(SmtpFilterTest, TestSendReplyDownstream) {
  initialize();


  EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
  Network::Connection::BytesSentCb cb;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));
  Buffer::OwnedImpl buf;
  EXPECT_CALL(connection_, write(_, false)).WillOnce(testing::SaveArg<0>(&buf));

  ASSERT_THAT(false, filter_->sendReplyDownstream(SmtpUtils::mailboxUnavailableResponse));

  ASSERT_STREQ(SmtpUtils::mailboxUnavailableResponse, buf.toString().c_str());

  filter_callbacks_.connection().close(Network::ConnectionCloseType::NoFlush);
  

  ASSERT_THAT(true, filter_->sendReplyDownstream(SmtpUtils::mailboxUnavailableResponse));

}

TEST_F(SmtpFilterTest,TestUpstreamStartTls) {
  initialize();

  //Upstream TLS is disabled, testing only downstream starttls handling
  filter_->getConfig()->upstream_tls_ =
      envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::REQUIRE;
  ASSERT_TRUE(filter_->upstreamTlsRequired());
  filter_->getSession().setState(SmtpSession::State::CONNECTION_SUCCESS);

  data_.add("EHLO localhost\r\n");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, filter_->getSession().getState());

  data_.drain(data_.length());

  data_.add("250-Hello localhost\r\n250-PIPELINING\r\n250-8BITMIME\r\n250-STARTTLS\r\n");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onWrite(data_, false));
  EXPECT_EQ(SmtpSession::State::SESSION_IN_PROGRESS, filter_->getSession().getState());
  data_.drain(data_.length());
  data_.add("STARTTLS\r\n");
  
  //Upstream TLS termination successful after STARTTLS
  
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, false));
  ASSERT_EQ(SmtpSession::State::UPSTREAM_TLS_NEGOTIATION, filter_->getSession().getState());

  data_.drain(data_.length());
  data_.add("220 Ready to start TLS\r\n");

  // EXPECT_CALL(filter_callbacks_, connection()).WillRepeatedly(ReturnRef(connection_));
  // Network::Connection::BytesSentCb cb;
  // EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&cb));
  // Buffer::OwnedImpl buf;
  // EXPECT_CALL(connection_, write(_, false)).WillOnce(testing::SaveArg<0>(&buf));

  EXPECT_CALL(filter_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(true));

  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onWrite(data_, false));

  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_EQ(config_->stats().sessions_upstream_tls_success_.value(), 1);

  filter_->getSession().setSessionEncrypted(false);
  filter_->getSession().setState(SmtpSession::State::UPSTREAM_TLS_NEGOTIATION);

  data_.drain(data_.length());
  data_.add("220 Ready to start TLS\r\n");

  EXPECT_CALL(filter_callbacks_, startUpstreamSecureTransport()).WillOnce(testing::Return(false));

  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onWrite(data_, false));
  ASSERT_EQ(SmtpSession::State::SESSION_TERMINATED, filter_->getSession().getState());
  // EXPECT_CALL(connection_, close(_)).Times(1);
  EXPECT_EQ(config_->stats().sessions_upstream_tls_failed_.value(), 1);


}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy