#include <string>

#include "envoy/config/filter/network/source_ip_access/v2/source_ip_access.pb.validate.h"
#include "envoy/stats/stats.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/source_ip_access/source_ip_access.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SourceIpAccess {

class SourceIpAccessFilterTest : public testing::Test {
public:
  SourceIpAccessFilterTest() {}

  void loadConfig(std::string json) {
    envoy::config::filter::network::source_ip_access::v2::SourceIpAccess proto_config{};
    MessageUtil::loadFromJson(json, proto_config);
    config_.reset(new Config(proto_config, stats_store_));
    filter_.reset(new Filter(config_));

    filter_callbacks_.connection_.remote_address_ =
        std::make_shared<Network::Address::Ipv4Instance>("10.0.0.3");
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  void runWith(std::string address_prefix, std::string prefix_len, bool allow) {
    std::string json_string = fmt::format(R"EOF(
  {{
    "stat_prefix": "my_stat_prefix",
    "allow_by_default": "false",
    "exception_prefixes": {{ "address_prefix": "{}", "prefix_len": "{}" }}
  }}
  )EOF",
                                          address_prefix, prefix_len);

    loadConfig(json_string);

    auto result = allow ? Network::FilterStatus::Continue : Network::FilterStatus::StopIteration;

    Buffer::OwnedImpl data("hello");
    EXPECT_EQ(result, filter_->onNewConnection());
    EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
  }

  void verifyStats(int ok, int denied) {
    EXPECT_EQ(ok, stats_store_.counter("source_ip_access.my_stat_prefix.allowed").value());
    EXPECT_EQ(denied, stats_store_.counter("source_ip_access.my_stat_prefix.denied").value());
    EXPECT_EQ(ok + denied, stats_store_.counter("source_ip_access.my_stat_prefix.total").value());
  }

  Stats::IsolatedStoreImpl stats_store_;
  ConfigSharedPtr config_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

TEST_F(SourceIpAccessFilterTest, AllowConfig) {
  runWith("10.0.0.3", "32", true);
  verifyStats(1, 0);
}

TEST_F(SourceIpAccessFilterTest, DenyConfig) {
  runWith("10.0.0.4", "32", false);
  verifyStats(0, 1);
}

TEST_F(SourceIpAccessFilterTest, AllowLargePrefixConfig) {
  runWith("10.0.0.0", "16", true);
  verifyStats(1, 0);
}

TEST_F(SourceIpAccessFilterTest, DenyLargePrefixConfig) {
  runWith("11.0.0.0", "16", false);
  verifyStats(0, 1);
}

TEST_F(SourceIpAccessFilterTest, BadConfig) {
  EXPECT_THROW(runWith("10.0.0", "32", false), EnvoyException);
}

} // namespace SourceIpAccess
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
