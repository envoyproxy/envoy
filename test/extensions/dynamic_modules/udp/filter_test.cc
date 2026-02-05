#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/filters/udp/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

class DynamicModuleUdpListenerFilterTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter
        proto_config;
    proto_config.set_filter_name("test_filter");
    proto_config.mutable_filter_config()->set_value("some_config");

    filter_config_ = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
        proto_config, std::move(dynamic_module.value()), *stats_.rootScope());
  }

  Stats::IsolatedStoreImpl stats_;
  DynamicModuleUdpListenerFilterConfigSharedPtr filter_config_;
};

TEST_F(DynamicModuleUdpListenerFilterTest, BasicDataFlow) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("hello");
  // Set addresses to avoid null dereferences if ABI accesses them
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");
  data.addresses_.local_ = Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:5678");

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data));

  // Verify buffer is cleared after callbacks.
  EXPECT_EQ(nullptr, filter->currentData());
}

TEST_F(DynamicModuleUdpListenerFilterTest, ReceiveError) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  // Just check it doesn't crash
  EXPECT_EQ(Network::FilterStatus::Continue,
            filter->onReceiveError(Api::IoError::IoErrorCode::UnknownError));
}

TEST_F(DynamicModuleUdpListenerFilterTest, ConfigMissingSymbols) {
  // Use the no_op module which lacks UDP symbols.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test_filter");

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<DynamicModuleUdpListenerFilterConfig>(
          proto_config, std::move(dynamic_module.value()), *stats_.rootScope()),
      EnvoyException,
      "Dynamic module does not support UDP listener filters: Failed to resolve symbol "
      "envoy_dynamic_module_on_udp_listener_filter_config_new");
}

TEST_F(DynamicModuleUdpListenerFilterTest, NullInModuleFilter) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;

  // Create a separate config that returns null from on_filter_new.
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test_filter");
  proto_config.mutable_filter_config()->set_value("config");

  auto bad_filter_config = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
      proto_config, std::move(dynamic_module.value()), *stats_.rootScope());

  // Replace the on_filter_new function to return null.
  auto null_returner = +[](envoy_dynamic_module_type_udp_listener_filter_config_module_ptr,
                           envoy_dynamic_module_type_udp_listener_filter_envoy_ptr)
      -> envoy_dynamic_module_type_udp_listener_filter_module_ptr { return nullptr; };
  bad_filter_config->on_filter_new_ = null_returner;

  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, bad_filter_config, 1);

  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  // Should return Continue when in_module_filter is null.
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data));
}

TEST_F(DynamicModuleUdpListenerFilterTest, EmptyBuffer) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>();
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data));
  EXPECT_EQ(nullptr, filter->currentData());
}

TEST_F(DynamicModuleUdpListenerFilterTest, LargeDataPayload) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  std::string large_data(65000, 'x');
  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>(large_data);
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(data));
  EXPECT_EQ(nullptr, filter->currentData());
}

TEST_F(DynamicModuleUdpListenerFilterTest, MultipleReceiveErrors) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  EXPECT_EQ(Network::FilterStatus::Continue,
            filter->onReceiveError(Api::IoError::IoErrorCode::NoSupport));
  EXPECT_EQ(Network::FilterStatus::Continue,
            filter->onReceiveError(Api::IoError::IoErrorCode::Again));
  EXPECT_EQ(Network::FilterStatus::Continue,
            filter->onReceiveError(Api::IoError::IoErrorCode::Permission));
}

TEST_F(DynamicModuleUdpListenerFilterTest, FilterConfigWithEmptyName) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("");
  proto_config.mutable_filter_config()->set_value("config");

  auto config = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
      proto_config, std::move(dynamic_module.value()), *stats_.rootScope());
  EXPECT_EQ("", config->filter_name_);
}

TEST_F(DynamicModuleUdpListenerFilterTest, FilterConfigWithNoConfig) {
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test");
  // No filter_config set.

  auto config = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
      proto_config, std::move(dynamic_module.value()), *stats_.rootScope());
  EXPECT_FALSE(config->filter_config_.empty());
}

TEST_F(DynamicModuleUdpListenerFilterTest, MultipleFiltersShareConfig) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks1;
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks2;

  auto filter1 = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks1, filter_config_, 1);
  auto filter2 = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks2, filter_config_, 1);

  Network::UdpRecvData data1;
  data1.buffer_ = std::make_unique<Buffer::OwnedImpl>("data1");
  data1.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  Network::UdpRecvData data2;
  data2.buffer_ = std::make_unique<Buffer::OwnedImpl>("data2");
  data2.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:5678");

  EXPECT_EQ(Network::FilterStatus::Continue, filter1->onData(data1));
  EXPECT_EQ(Network::FilterStatus::Continue, filter2->onData(data2));
}

TEST_F(DynamicModuleUdpListenerFilterTest, CallbacksAccessor) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  EXPECT_EQ(&callbacks, filter->callbacks());
}

TEST_F(DynamicModuleUdpListenerFilterTest, CurrentDataAccessor) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  EXPECT_EQ(nullptr, filter->currentData());

  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  filter->onData(data);
  EXPECT_EQ(nullptr, filter->currentData());
}

class DynamicModuleUdpListenerFilterStopIterationTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
        Extensions::DynamicModules::testSharedObjectPath("udp_stop_iteration", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter
        proto_config;
    proto_config.set_filter_name("stop_filter");
    proto_config.mutable_filter_config()->set_value("config");

    filter_config_ = std::make_shared<DynamicModuleUdpListenerFilterConfig>(
        proto_config, std::move(dynamic_module.value()), *stats_.rootScope());
  }

  Stats::IsolatedStoreImpl stats_;
  DynamicModuleUdpListenerFilterConfigSharedPtr filter_config_;
};

TEST_F(DynamicModuleUdpListenerFilterStopIterationTest, ReturnsStopIteration) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_unique<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  Network::UdpRecvData data;
  data.buffer_ = std::make_unique<Buffer::OwnedImpl>("test");
  data.addresses_.peer_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:1234");

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data));
}

// Test for missing config_destroy symbol.
TEST(DynamicModuleUdpListenerFilterConfigErrorTest, MissingConfigDestroy) {
  Stats::IsolatedStoreImpl stats;
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_config_destroy", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test");
  proto_config.mutable_filter_config()->set_value("config");

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<DynamicModuleUdpListenerFilterConfig>(
          proto_config, std::move(dynamic_module.value()), *stats.rootScope()),
      EnvoyException,
      "Dynamic module does not support UDP listener filters: Failed to resolve symbol "
      "envoy_dynamic_module_on_udp_listener_filter_config_destroy");
}

// Test for missing filter_new symbol.
TEST(DynamicModuleUdpListenerFilterConfigErrorTest, MissingFilterNew) {
  Stats::IsolatedStoreImpl stats;
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_filter_new", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test");

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<DynamicModuleUdpListenerFilterConfig>(
          proto_config, std::move(dynamic_module.value()), *stats.rootScope()),
      EnvoyException,
      "Dynamic module does not support UDP listener filters: Failed to resolve symbol "
      "envoy_dynamic_module_on_udp_listener_filter_new");
}

// Test for missing on_data symbol.
TEST(DynamicModuleUdpListenerFilterConfigErrorTest, MissingOnData) {
  Stats::IsolatedStoreImpl stats;
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_on_data", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test");

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<DynamicModuleUdpListenerFilterConfig>(
          proto_config, std::move(dynamic_module.value()), *stats.rootScope()),
      EnvoyException,
      "Dynamic module does not support UDP listener filters: Failed to resolve symbol "
      "envoy_dynamic_module_on_udp_listener_filter_on_data");
}

// Test for missing filter_destroy symbol.
TEST(DynamicModuleUdpListenerFilterConfigErrorTest, MissingFilterDestroy) {
  Stats::IsolatedStoreImpl stats;
  auto dynamic_module = Extensions::DynamicModules::newDynamicModule(
      Extensions::DynamicModules::testSharedObjectPath("udp_no_filter_destroy", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter proto_config;
  proto_config.set_filter_name("test");

  EXPECT_THROW_WITH_MESSAGE(
      std::make_shared<DynamicModuleUdpListenerFilterConfig>(
          proto_config, std::move(dynamic_module.value()), *stats.rootScope()),
      EnvoyException,
      "Dynamic module does not support UDP listener filters: Failed to resolve symbol "
      "envoy_dynamic_module_on_udp_listener_filter_destroy");
}

// =================================================================================================
// Metrics ABI Tests
// =================================================================================================

TEST_F(DynamicModuleUdpListenerFilterTest, MetricsCounterDefineAndIncrement) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  // Define a counter via the config.
  size_t counter_id = 0;
  auto result = envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
      static_cast<void*>(filter_config_.get()),
      {const_cast<char*>("test_counter"), strlen("test_counter")}, &counter_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);
  EXPECT_EQ(0, counter_id);

  // Increment the counter via the filter.
  result = envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
      static_cast<void*>(filter.get()), counter_id, 5);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);

  // Verify the counter value.
  auto counter = TestUtility::findCounter(stats_, "dynamicmodulescustom.test_filter.test_counter");
  ASSERT_NE(nullptr, counter);
  EXPECT_EQ(5, counter->value());
}

TEST_F(DynamicModuleUdpListenerFilterTest, MetricsGaugeDefineAndOperations) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  // Define a gauge.
  size_t gauge_id = 0;
  auto result = envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
      static_cast<void*>(filter_config_.get()),
      {const_cast<char*>("test_gauge"), strlen("test_gauge")}, &gauge_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);

  // Set gauge value.
  result = envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
      static_cast<void*>(filter.get()), gauge_id, 100);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);

  // Increment gauge.
  result = envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(
      static_cast<void*>(filter.get()), gauge_id, 10);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);

  // Decrement gauge.
  result = envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(
      static_cast<void*>(filter.get()), gauge_id, 5);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);

  // Verify gauge value.
  auto gauge = TestUtility::findGauge(stats_, "dynamicmodulescustom.test_filter.test_gauge");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(105, gauge->value());
}

TEST_F(DynamicModuleUdpListenerFilterTest, MetricsHistogramDefineAndRecord) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  // Define a histogram.
  size_t histogram_id = 0;
  auto result = envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
      static_cast<void*>(filter_config_.get()),
      {const_cast<char*>("test_histogram"), strlen("test_histogram")}, &histogram_id);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);

  // Record a value. This doesn't crash.
  result = envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
      static_cast<void*>(filter.get()), histogram_id, 42);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success, result);
}

TEST_F(DynamicModuleUdpListenerFilterTest, MetricsNotFound) {
  NiceMock<Network::MockUdpReadFilterCallbacks> callbacks;
  auto filter = std::make_shared<DynamicModuleUdpListenerFilter>(callbacks, filter_config_, 1);

  // Try to increment a counter that doesn't exist.
  auto result = envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
      static_cast<void*>(filter.get()), 999, 1);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound, result);

  // Try to set a gauge that doesn't exist.
  result = envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
      static_cast<void*>(filter.get()), 999, 1);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound, result);

  // Try to record a histogram that doesn't exist.
  result = envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
      static_cast<void*>(filter.get()), 999, 1);
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound, result);
}

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
