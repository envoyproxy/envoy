#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"
#include "source/extensions/filters/listener/dynamic_modules/filter_config.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace ListenerFilters {

// A simple mock implementation of ListenerFilterBuffer for testing.
class TestListenerFilterBuffer : public Network::ListenerFilterBuffer {
public:
  TestListenerFilterBuffer(Buffer::Instance& buffer) : buffer_(buffer) {}

  const Buffer::ConstRawSlice rawSlice() const override {
    Buffer::RawSliceVector slices = buffer_.getRawSlices();
    if (slices.empty()) {
      return {nullptr, 0};
    }
    return {slices[0].mem_, slices[0].len_};
  }

  bool drain(uint64_t length) override {
    if (length > buffer_.length()) {
      length = buffer_.length();
    }
    buffer_.drain(length);
    return true;
  }

private:
  Buffer::Instance& buffer_;
};

class DynamicModuleListenerFilterTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("listener_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status =
        newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()),
                                             *stats_.rootScope(), main_thread_dispatcher_);
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();

    ON_CALL(callbacks_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher));
  }

  Stats::IsolatedStoreImpl stats_;
  DynamicModuleListenerFilterConfigSharedPtr filter_config_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
};

TEST_F(DynamicModuleListenerFilterTest, BasicFilterFlow) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks_));
  EXPECT_EQ(&callbacks_, filter->callbacks());
}

TEST_F(DynamicModuleListenerFilterTest, MaxReadBytes) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  // The no_op module returns 0 for maxReadBytes.
  EXPECT_EQ(0, filter->maxReadBytes());
}

TEST_F(DynamicModuleListenerFilterTest, OnCloseDoesNotCrash) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  // onClose should not crash.
  filter->onClose();
}

TEST_F(DynamicModuleListenerFilterTest, FilterDestroyWithIsDestroyedCheck) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  EXPECT_FALSE(filter->isDestroyed());

  // Explicitly destroy the filter by letting it go out of scope.
  filter.reset();
}

TEST_F(DynamicModuleListenerFilterTest, FilterDestroyWithoutInitialization) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  // We are deliberately not calling initializeInModuleFilter().

  EXPECT_FALSE(filter->isDestroyed());

  // Destroy the filter without ever initializing it.
  filter.reset();
}

TEST_F(DynamicModuleListenerFilterTest, FilterWithNullInModuleFilterOnClose) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  // Deliberately not calling initializeInModuleFilter().

  // onClose should not crash with null in_module_filter_.
  filter->onClose();
}

TEST_F(DynamicModuleListenerFilterTest, OnAcceptWithNullInModuleFilterClosesSocket) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_filter_new_fail", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()),
                                           *stats_.rootScope(), main_thread_dispatcher_);
  EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
  auto filter_config = filter_config_or_status.value();

  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config);

  // Create a real mock io handle so we can verify close is called.
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  auto* mock_io_handle_ptr = mock_io_handle.get();
  EXPECT_CALL(*mock_io_handle_ptr, close())
      .WillOnce(testing::Return(Api::IoCallUint64Result(0, Api::IoError::none())));

  // Replace the io_handle and update the ioHandle() mock to return a reference to it.
  callbacks_.socket_.io_handle_ = std::move(mock_io_handle);
  ON_CALL(callbacks_.socket_, ioHandle())
      .WillByDefault(testing::ReturnRef(*callbacks_.socket_.io_handle_));

  // When in_module_filter_ is null, onAccept should close the socket and return StopIteration.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onAccept(callbacks_));
}

TEST_F(DynamicModuleListenerFilterTest, OnDataWithNullInModuleFilter) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  // Deliberately not calling initializeInModuleFilter().

  Buffer::OwnedImpl buffer("test data");
  TestListenerFilterBuffer test_buffer(buffer);

  // When in_module_filter_ is null, onData should return Continue.
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(test_buffer));
}

TEST_F(DynamicModuleListenerFilterTest, OnDataWithBuffer) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);

  filter->onAccept(callbacks_);

  Buffer::OwnedImpl buffer("test data");
  TestListenerFilterBuffer test_buffer(buffer);

  // The no_op module returns Continue for onData.
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(test_buffer));

  // currentBuffer should be null after onData completes.
  EXPECT_EQ(nullptr, filter->currentBuffer());
}

TEST_F(DynamicModuleListenerFilterTest, FilterWithNullInModuleFilterMaxReadBytes) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  // Deliberately not calling initializeInModuleFilter().

  // maxReadBytes should return 0 when in_module_filter_ is null.
  EXPECT_EQ(0, filter->maxReadBytes());
}

TEST_F(DynamicModuleListenerFilterTest, CallbackAccessor) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);

  // Before onAccept, callbacks should be null.
  EXPECT_EQ(nullptr, filter->callbacks());

  NiceMock<Network::MockListenerFilterCallbacks> callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks_, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher));
  filter->onAccept(callbacks_);

  // After onAccept, callbacks should be set.
  EXPECT_EQ(&callbacks_, filter->callbacks());
}

TEST_F(DynamicModuleListenerFilterTest, GetFilterConfig) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  // Verify getFilterConfig() returns the correct config.
  const auto& config = filter->getFilterConfig();
  EXPECT_NE(nullptr, config.in_module_config_);
}

TEST(DynamicModuleListenerFilterConfigTest, ConfigInitialization) {
  Stats::IsolatedStoreImpl stats;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto dynamic_module = newDynamicModule(testSharedObjectPath("listener_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status = newDynamicModuleListenerFilterConfig(
      "test_filter", "some_config", std::move(dynamic_module.value()), *stats.rootScope(),
      main_thread_dispatcher);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto config = filter_config_or_status.value();
  EXPECT_NE(nullptr, config->in_module_config_);
  EXPECT_NE(nullptr, config->on_listener_filter_config_destroy_);
  EXPECT_NE(nullptr, config->on_listener_filter_new_);
  EXPECT_NE(nullptr, config->on_listener_filter_on_accept_);
  EXPECT_NE(nullptr, config->on_listener_filter_on_data_);
  EXPECT_NE(nullptr, config->on_listener_filter_on_close_);
  EXPECT_NE(nullptr, config->on_listener_filter_get_max_read_bytes_);
  EXPECT_NE(nullptr, config->on_listener_filter_destroy_);
}

TEST(DynamicModuleListenerFilterConfigTest, MissingSymbols) {
  Stats::IsolatedStoreImpl stats;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  // Use the HTTP filter no_op module which lacks listener filter symbols.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()),
                                           *stats.rootScope(), main_thread_dispatcher);
  EXPECT_FALSE(filter_config_or_status.ok());
}

TEST(DynamicModuleListenerFilterConfigTest, ConfigInitializationFailure) {
  Stats::IsolatedStoreImpl stats;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  // Use a module that returns nullptr from config_new.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_config_new_fail", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()),
                                           *stats.rootScope(), main_thread_dispatcher);
  EXPECT_FALSE(filter_config_or_status.ok());
  EXPECT_THAT(filter_config_or_status.status().message(),
              testing::HasSubstr("Failed to initialize"));
}

TEST(DynamicModuleListenerFilterConfigTest, StopIterationStatus) {
  Stats::IsolatedStoreImpl stats;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_stop_iteration", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()),
                                           *stats.rootScope(), main_thread_dispatcher);
  EXPECT_TRUE(filter_config_or_status.ok());
  auto config = filter_config_or_status.value();

  auto filter = std::make_unique<DynamicModuleListenerFilter>(config);

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher));

  // onAccept should return StopIteration.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onAccept(callbacks));

  // maxReadBytes should return 1024 (value defined in listener_stop_iteration.c).
  EXPECT_EQ(1024, filter->maxReadBytes());
}

TEST(DynamicModuleListenerFilterConfigTest, OnDataStopIterationStatus) {
  Stats::IsolatedStoreImpl stats;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_stop_iteration", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()),
                                           *stats.rootScope(), main_thread_dispatcher);
  EXPECT_TRUE(filter_config_or_status.ok());
  auto config = filter_config_or_status.value();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  NiceMock<Event::MockDispatcher> dispatcher{"worker_0"};
  ON_CALL(callbacks, dispatcher()).WillByDefault(testing::ReturnRef(dispatcher));

  auto filter = std::make_unique<DynamicModuleListenerFilter>(config);
  filter->onAccept(callbacks);

  Buffer::OwnedImpl buffer("test data");
  TestListenerFilterBuffer test_buffer(buffer);

  // onData should return StopIteration for the stop_iteration module.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(test_buffer));
}

TEST_F(DynamicModuleListenerFilterTest, MetricsCounterDefineAndIncrement) {
  // Test that we can define and increment a counter via the config.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_counter", .length = 12};
  size_t counter_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_config_define_counter(
                static_cast<void*>(filter_config_.get()), name, &counter_id));
  EXPECT_EQ(0, counter_id);

  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_increment_counter(
                static_cast<void*>(filter.get()), counter_id, 5));

  // Verify the counter value.
  auto counter = TestUtility::findCounter(stats_, "dynamic_module_listener_filter.test_counter");
  ASSERT_NE(nullptr, counter);
  EXPECT_EQ(5, counter->value());
}

TEST_F(DynamicModuleListenerFilterTest, MetricsGaugeDefineAndManipulate) {
  // Test that we can define and manipulate a gauge via the config.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_gauge", .length = 10};
  size_t gauge_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_config_define_gauge(
                static_cast<void*>(filter_config_.get()), name, &gauge_id));
  EXPECT_EQ(0, gauge_id);

  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_set_gauge(
                static_cast<void*>(filter.get()), gauge_id, 100));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_increment_gauge(
                static_cast<void*>(filter.get()), gauge_id, 10));

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_decrement_gauge(
                static_cast<void*>(filter.get()), gauge_id, 5));

  // Verify the gauge value: 100 + 10 - 5 = 105.
  auto gauge = TestUtility::findGauge(stats_, "dynamic_module_listener_filter.test_gauge");
  ASSERT_NE(nullptr, gauge);
  EXPECT_EQ(105, gauge->value());
}

TEST_F(DynamicModuleListenerFilterTest, MetricsHistogramDefineAndRecord) {
  // Test that we can define and record values in a histogram via the config.
  envoy_dynamic_module_type_module_buffer name = {.ptr = "test_histogram", .length = 14};
  size_t histogram_id = 0;

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_config_define_histogram(
                static_cast<void*>(filter_config_.get()), name, &histogram_id));
  EXPECT_EQ(0, histogram_id);

  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);
  filter->setCallbacksForTest(nullptr);

  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_Success,
            envoy_dynamic_module_callback_listener_filter_record_histogram_value(
                static_cast<void*>(filter.get()), histogram_id, 42));

  // Histograms don't expose a simple value to check, but we verify no error.
}

TEST_F(DynamicModuleListenerFilterTest, MetricsInvalidId) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->onAccept(callbacks_);

  // Test that using an invalid ID returns an error.
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_listener_filter_increment_counter(
                static_cast<void*>(filter.get()), 999, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_listener_filter_set_gauge(
                static_cast<void*>(filter.get()), 999, 1));
  EXPECT_EQ(envoy_dynamic_module_type_metrics_result_MetricNotFound,
            envoy_dynamic_module_callback_listener_filter_record_histogram_value(
                static_cast<void*>(filter.get()), 999, 1));
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
