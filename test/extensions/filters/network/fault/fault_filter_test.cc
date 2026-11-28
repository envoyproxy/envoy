#include <memory>
#include <string>

#include "envoy/extensions/filters/network/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/network/fault/v3/fault.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/fault/fault_filter.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {
namespace {

class NetworkFaultFilterTest : public testing::Test {
public:
  void setUpFilter(const std::string& yaml) {
    envoy::extensions::filters::network::fault::v3::NetworkFault proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);
    setUpFilter(proto_config);
  }

  void
  setUpFilter(const envoy::extensions::filters::network::fault::v3::NetworkFault& proto_config) {
    EXPECT_CALL(filter_callbacks_, connection())
        .WillRepeatedly(ReturnRef(filter_callbacks_.connection_));
    EXPECT_CALL(filter_callbacks_.connection_, streamInfo())
        .WillRepeatedly(ReturnRef(stream_info_));

    config_ =
        std::make_shared<FaultFilterConfig>(proto_config, *stats_store_.rootScope(), runtime_);
    filter_ = std::make_unique<FaultFilter>(config_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  FaultFilterConfigSharedPtr config_;
  std::unique_ptr<FaultFilter> filter_;
};

TEST_F(NetworkFaultFilterTest, NoFaultConfiguredPassesThrough) {
  setUpFilter(R"EOF(
stat_prefix: tcp
)EOF");

  EXPECT_CALL(filter_callbacks_.connection_, close(_, _)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ(0U, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0U, config_->stats().aborts_dry_run_.value());
  EXPECT_FALSE(stream_info_.hasResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));
  EXPECT_TRUE(stream_info_.dynamicMetadata().filter_metadata().empty());
}

TEST_F(NetworkFaultFilterTest, ZeroPercentAbortPassesThrough) {
  setUpFilter(R"EOF(
stat_prefix: tcp
abort:
  percentage:
    numerator: 0
    denominator: HUNDRED
filter_metadata:
  experiment_name: "test_experiment"
)EOF");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_, close(_, _)).Times(0);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  EXPECT_EQ(0U, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0U, config_->stats().aborts_dry_run_.value());
  EXPECT_FALSE(stream_info_.hasResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));
  // Unmatched connections must not populate dynamic metadata.
  EXPECT_TRUE(stream_info_.dynamicMetadata().filter_metadata().empty());
}

TEST_F(NetworkFaultFilterTest, EnforcedAbortClosesConnectionAndRecordsMetadata) {
  setUpFilter(R"EOF(
stat_prefix: tcp
abort:
  percentage:
    numerator: 100
    denominator: HUNDRED
filter_metadata:
  experiment_name: "test_experiment"
  fault_name: "fault0"
)EOF");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush,
                                                   absl::string_view("fault_filter_abort")));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  // Duplicate onNewConnection() or subsequent onData() calls remain idempotent and stopped.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_EQ("fault.tcp.aborts_injected", config_->stats().aborts_injected_.name());
  EXPECT_EQ(1U, config_->stats().aborts_injected_.value());
  EXPECT_EQ(0U, config_->stats().aborts_dry_run_.value());
  EXPECT_TRUE(stream_info_.hasResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));
  ASSERT_TRUE(stream_info_.responseCodeDetails().has_value());
  EXPECT_EQ("fault_filter_abort", stream_info_.responseCodeDetails().value());
  ASSERT_TRUE(stream_info_.connectionTerminationDetails().has_value());
  EXPECT_EQ("fault_filter_abort", stream_info_.connectionTerminationDetails().value());

  const auto& filter_meta = stream_info_.dynamicMetadata().filter_metadata();
  const auto it = filter_meta.find(NetworkFilterNames::get().Fault);
  ASSERT_NE(it, filter_meta.end());
  EXPECT_EQ("test_experiment", it->second.fields().at("experiment_name").string_value());
  EXPECT_EQ("fault0", it->second.fields().at("fault_name").string_value());
}

TEST_F(NetworkFaultFilterTest, EnforcedAbortWithoutMetadataDoesNotSetDynamicMetadata) {
  setUpFilter(R"EOF(
stat_prefix: tcp.
abort:
  percentage:
    numerator: 100
    denominator: HUNDRED
)EOF");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush,
                                                   absl::string_view("fault_filter_abort")));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  // Trailing dot on stat_prefix is normalized cleanly.
  EXPECT_EQ("fault.tcp.aborts_injected", config_->stats().aborts_injected_.name());
  EXPECT_EQ(1U, config_->stats().aborts_injected_.value());
  EXPECT_TRUE(stream_info_.dynamicMetadata().filter_metadata().empty());
}

TEST_F(NetworkFaultFilterTest, EnforcedAbortWithEmptyFilterMetadataDoesNotSetDynamicMetadata) {
  envoy::extensions::filters::network::fault::v3::NetworkFault proto_config;
  TestUtility::loadFromYamlAndValidate(R"EOF(
stat_prefix: tcp
abort:
  percentage:
    numerator: 100
    denominator: HUNDRED
filter_metadata: {}
)EOF",
                                       proto_config);
  ASSERT_TRUE(proto_config.has_filter_metadata());
  ASSERT_TRUE(proto_config.filter_metadata().fields().empty());
  setUpFilter(proto_config);

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush,
                                                   absl::string_view("fault_filter_abort")));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  EXPECT_EQ(1U, config_->stats().aborts_injected_.value());
  EXPECT_TRUE(stream_info_.dynamicMetadata().filter_metadata().empty());
}

TEST_F(NetworkFaultFilterTest, EnforcedAbortMergesIntoExistingDynamicMetadata) {
  setUpFilter(R"EOF(
stat_prefix: tcp
abort:
  percentage:
    numerator: 100
    denominator: HUNDRED
filter_metadata:
  experiment_name: "test_experiment"
)EOF");

  // Pre-populate dynamic metadata under the fault namespace (e.g., from another fault/filter).
  auto& existing =
      (*stream_info_.dynamicMetadata().mutable_filter_metadata())[NetworkFilterNames::get().Fault];
  (*existing.mutable_fields())["existing_key"].set_string_value("existing_value");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush,
                                                   absl::string_view("fault_filter_abort")));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());

  const auto& filter_meta = stream_info_.dynamicMetadata().filter_metadata();
  const auto it = filter_meta.find(NetworkFilterNames::get().Fault);
  ASSERT_NE(it, filter_meta.end());
  EXPECT_EQ("existing_value", it->second.fields().at("existing_key").string_value());
  EXPECT_EQ("test_experiment", it->second.fields().at("experiment_name").string_value());
}

TEST_F(NetworkFaultFilterTest, DryRunAbortContinuesWithoutClosingConnectionOrSettingResponseFlag) {
  setUpFilter(R"EOF(
stat_prefix: tcp
abort:
  percentage:
    numerator: 100
    denominator: HUNDRED
  dry_run: true
filter_metadata:
  experiment_name: "test_experiment"
  fault_name: "fault0"
)EOF");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(_, _)).Times(0);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
  // Duplicate onNewConnection() is idempotent and does not double-count dry_run stats.
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  EXPECT_EQ("fault.tcp.aborts_dry_run", config_->stats().aborts_dry_run_.name());
  EXPECT_EQ(0U, config_->stats().aborts_injected_.value());
  EXPECT_EQ(1U, config_->stats().aborts_dry_run_.value());
  EXPECT_FALSE(stream_info_.hasResponseFlag(StreamInfo::CoreResponseFlag::FaultInjected));
  EXPECT_FALSE(stream_info_.responseCodeDetails().has_value());
  EXPECT_FALSE(stream_info_.connectionTerminationDetails().has_value());

  const auto& filter_meta = stream_info_.dynamicMetadata().filter_metadata();
  const auto it = filter_meta.find(NetworkFilterNames::get().Fault);
  ASSERT_NE(it, filter_meta.end());
  EXPECT_EQ(1, it->second.fields_size());
  ASSERT_TRUE(it->second.fields().contains("dry_run_abort"));
  EXPECT_TRUE(it->second.fields().at("dry_run_abort").bool_value());
  // User-configured filter_metadata must not be written for dry-run abort matches.
  EXPECT_FALSE(it->second.fields().contains("experiment_name"));
  EXPECT_FALSE(it->second.fields().contains("fault_name"));
}

TEST_F(NetworkFaultFilterTest, DryRunAbortMergesIntoExistingDynamicMetadata) {
  setUpFilter(R"EOF(
stat_prefix: tcp
abort:
  percentage:
    numerator: 100
    denominator: HUNDRED
  dry_run: true
filter_metadata:
  experiment_name: "test_experiment"
)EOF");

  // Pre-populate dynamic metadata under the fault namespace (e.g., from another fault/filter).
  auto& existing =
      (*stream_info_.dynamicMetadata().mutable_filter_metadata())[NetworkFilterNames::get().Fault];
  (*existing.mutable_fields())["existing_key"].set_string_value("existing_value");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("fault.network.abort.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_, close(_, _)).Times(0);

  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());

  const auto& filter_meta = stream_info_.dynamicMetadata().filter_metadata();
  const auto it = filter_meta.find(NetworkFilterNames::get().Fault);
  ASSERT_NE(it, filter_meta.end());
  EXPECT_EQ("existing_value", it->second.fields().at("existing_key").string_value());
  EXPECT_TRUE(it->second.fields().at("dry_run_abort").bool_value());
  EXPECT_FALSE(it->second.fields().contains("experiment_name"));
}

TEST_F(NetworkFaultFilterTest, CustomAbortPercentRuntimeKeyUsed) {
  setUpFilter(R"EOF(
stat_prefix: tcp
abort_percent_runtime: custom.fault.abort_percent
abort:
  percentage:
    numerator: 25000
    denominator: MILLION
)EOF");

  EXPECT_CALL(runtime_.snapshot_,
              featureEnabled(absl::string_view("custom.fault.abort_percent"),
                             testing::Matcher<const envoy::type::v3::FractionalPercent&>(_)))
      .WillOnce([](absl::string_view, const envoy::type::v3::FractionalPercent& default_value) {
        EXPECT_EQ(25000U, default_value.numerator());
        EXPECT_EQ(envoy::type::v3::FractionalPercent::MILLION, default_value.denominator());
        return true;
      });
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush,
                                                   absl::string_view("fault_filter_abort")));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onNewConnection());
  EXPECT_EQ(1U, config_->stats().aborts_injected_.value());
}

} // namespace
} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
