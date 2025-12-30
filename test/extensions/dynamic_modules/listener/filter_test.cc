#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/listener/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
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
        newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()));
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();
  }

  DynamicModuleListenerFilterConfigSharedPtr filter_config_;
};

TEST_F(DynamicModuleListenerFilterTest, BasicFilterFlow) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onAccept(callbacks));
  EXPECT_EQ(&callbacks, filter->callbacks());
}

TEST_F(DynamicModuleListenerFilterTest, MaxReadBytes) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // The no_op module returns 0 for maxReadBytes.
  EXPECT_EQ(0, filter->maxReadBytes());
}

TEST_F(DynamicModuleListenerFilterTest, OnCloseDoesNotCrash) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  filter->onAccept(callbacks);

  // onClose should not crash.
  filter->onClose();
}

TEST_F(DynamicModuleListenerFilterTest, FilterDestroyWithIsDestroyedCheck) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  filter->onAccept(callbacks);

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
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  // Deliberately not calling initializeInModuleFilter() to simulate a failure.

  // Create a real mock io handle so we can verify close is called.
  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  auto* mock_io_handle_ptr = mock_io_handle.get();
  EXPECT_CALL(*mock_io_handle_ptr, close())
      .WillOnce(testing::Return(Api::IoCallUint64Result(0, Api::IoError::none())));

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  // Replace the io_handle and update the ioHandle() mock to return a reference to it.
  callbacks.socket_.io_handle_ = std::move(mock_io_handle);
  ON_CALL(callbacks.socket_, ioHandle())
      .WillByDefault(testing::ReturnRef(*callbacks.socket_.io_handle_));

  // When in_module_filter_ is null, onAccept should close the socket and return StopIteration.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onAccept(callbacks));
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
  filter->initializeInModuleFilter();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  filter->onAccept(callbacks);

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
  filter->initializeInModuleFilter();

  // Before onAccept, callbacks should be null.
  EXPECT_EQ(nullptr, filter->callbacks());

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  filter->onAccept(callbacks);

  // After onAccept, callbacks should be set.
  EXPECT_EQ(&callbacks, filter->callbacks());
}

TEST_F(DynamicModuleListenerFilterTest, GetFilterConfig) {
  auto filter = std::make_unique<DynamicModuleListenerFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Verify getFilterConfig() returns the correct config.
  const auto& config = filter->getFilterConfig();
  EXPECT_NE(nullptr, config.in_module_config_);
}

TEST(DynamicModuleListenerFilterConfigTest, ConfigInitialization) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("listener_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status = newDynamicModuleListenerFilterConfig(
      "test_filter", "some_config", std::move(dynamic_module.value()));
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
  // Use the HTTP filter no_op module which lacks listener filter symbols.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()));
  EXPECT_FALSE(filter_config_or_status.ok());
}

TEST(DynamicModuleListenerFilterConfigTest, ConfigInitializationFailure) {
  // Use a module that returns nullptr from config_new.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_config_new_fail", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()));
  EXPECT_FALSE(filter_config_or_status.ok());
  EXPECT_THAT(filter_config_or_status.status().message(),
              testing::HasSubstr("Failed to initialize"));
}

TEST(DynamicModuleListenerFilterConfigTest, StopIterationStatus) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_stop_iteration", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()));
  EXPECT_TRUE(filter_config_or_status.ok());
  auto config = filter_config_or_status.value();

  auto filter = std::make_unique<DynamicModuleListenerFilter>(config);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;

  // onAccept should return StopIteration.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onAccept(callbacks));

  // maxReadBytes should return 1024 (value defined in listener_stop_iteration.c).
  EXPECT_EQ(1024, filter->maxReadBytes());
}

TEST(DynamicModuleListenerFilterConfigTest, OnDataStopIterationStatus) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("listener_stop_iteration", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  auto filter_config_or_status =
      newDynamicModuleListenerFilterConfig("test_filter", "", std::move(dynamic_module.value()));
  EXPECT_TRUE(filter_config_or_status.ok());
  auto config = filter_config_or_status.value();

  auto filter = std::make_unique<DynamicModuleListenerFilter>(config);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockListenerFilterCallbacks> callbacks;
  filter->onAccept(callbacks);

  Buffer::OwnedImpl buffer("test data");
  TestListenerFilterBuffer test_buffer(buffer);

  // onData should return StopIteration for the stop_iteration module.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(test_buffer));
}

} // namespace ListenerFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
