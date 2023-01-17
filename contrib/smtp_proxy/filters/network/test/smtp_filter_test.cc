#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_filter.h"
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
  EXPECT_EQ(1, config_->stats().smtp_session_requests_.value());
}

TEST_F(SmtpFilterTest, DownstreamStarttls) {
  initialize();

  filter_->getConfig()->upstream_tls_ =
      envoy::extensions::filters::network::smtp_proxy::v3alpha::SmtpProxy::DISABLE;
  ASSERT_FALSE(filter_->upstreamTlsRequired());
  filter_->getSession().setState(SmtpSession::State::CONNECTION_SUCCESS);
  
  data_.add("EHLO localhost\r\n");
  ASSERT_THAT(Network::FilterStatus::Continue, filter_->onData(data_, false));
  EXPECT_EQ(SmtpSession::State::SESSION_INIT_REQUEST, filter_->getSession().getState());

  data_.drain(data_.length());
  filter_->getSession().setState(SmtpSession::State::SESSION_IN_PROGRESS);
  data_.add("STARTTLS\r\n");
  
  ASSERT_THAT(Network::FilterStatus::StopIteration, filter_->onData(data_, false));
  EXPECT_EQ(SmtpSession::State::DOWNSTREAM_TLS_NEGOTIATION, filter_->getSession().getState());

}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy