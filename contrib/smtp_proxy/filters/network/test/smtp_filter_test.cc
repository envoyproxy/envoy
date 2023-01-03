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
  MySQLFilterTest() { ENVOY_LOG_MISC(info, "test"); }

  void initialize() {
    config_ = std::make_shared<SmtpFilterConfig>(stat_prefix_, scope_);
    filter_ = std::make_unique<SmtpFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  SmtpFilterConfigSharedPtr config_;
  std::unique_ptr<SmtpFilter> filter_;
  Stats::IsolatedStoreImpl scope_;
  std::string stat_prefix_{"test."};
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

// Test New Session counter increment
TEST_F(SmtpFilterTest, NewSessionStatsTest) {
  initialize();

  EXPECT_EQ(Envoy::Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(1, config_->stats().smtp_session_requests.value());
}
