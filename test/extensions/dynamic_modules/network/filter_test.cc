#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

class DynamicModuleNetworkFilterTest : public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
    EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

    auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
        "test_filter", "", std::move(dynamic_module.value()), cluster_manager_);
    EXPECT_TRUE(filter_config_or_status.ok()) << filter_config_or_status.status().message();
    filter_config_ = filter_config_or_status.value();
  }

  DynamicModuleNetworkFilterConfigSharedPtr filter_config_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
};

TEST_F(DynamicModuleNetworkFilterTest, BasicDataFlow) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Network::MockConnection> connection;

  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));

  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  Buffer::OwnedImpl read_data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(read_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(read_data, true));

  Buffer::OwnedImpl write_data("world");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onWrite(write_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onWrite(write_data, true));

  // Verify buffer is cleared after callbacks.
  EXPECT_EQ(nullptr, filter->currentReadBuffer());
  EXPECT_EQ(nullptr, filter->currentWriteBuffer());
}

TEST_F(DynamicModuleNetworkFilterTest, AllConnectionEvents) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  // Test all connection events.
  filter->onEvent(Network::ConnectionEvent::Connected);
  filter->onEvent(Network::ConnectionEvent::RemoteClose);
  filter->onEvent(Network::ConnectionEvent::LocalClose);
  filter->onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
}

TEST_F(DynamicModuleNetworkFilterTest, WatermarkCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // These should not crash.
  filter->onAboveWriteBufferHighWatermark();
  filter->onBelowWriteBufferLowWatermark();
}

TEST_F(DynamicModuleNetworkFilterTest, FilterDestroyWithIsDestroyedCheck) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  EXPECT_FALSE(filter->isDestroyed());

  // Explicitly destroy the filter by letting it go out of scope.
  filter.reset();
}

TEST_F(DynamicModuleNetworkFilterTest, FilterDestroyWithoutInitialization) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  // We are deliberately not calling initializeInModuleFilter().

  EXPECT_FALSE(filter->isDestroyed());

  // Destroy the filter without ever initializing it.
  filter.reset();
}

TEST_F(DynamicModuleNetworkFilterTest, FilterWithoutInModuleFilter) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  // Deliberately not calling initializeInModuleFilter().

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  // These should return StopIteration and close connection when in_module_filter_ is null.
  EXPECT_CALL(connection, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());

  Buffer::OwnedImpl read_data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(read_data, false));

  Buffer::OwnedImpl write_data("world");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onWrite(write_data, false));

  // onEvent should not crash with null in_module_filter_.
  filter->onEvent(Network::ConnectionEvent::Connected);
}

TEST_F(DynamicModuleNetworkFilterTest, ContinueReadingNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Without initializing callbacks, continueReading should not crash.
  filter->continueReading();
}

TEST_F(DynamicModuleNetworkFilterTest, WriteNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Without initializing callbacks, write should not crash.
  Buffer::OwnedImpl data("test");
  filter->write(data, false);
}

TEST_F(DynamicModuleNetworkFilterTest, CloseNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Without initializing callbacks, close should not crash.
  filter->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(DynamicModuleNetworkFilterTest, ContinueReadingWithCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  EXPECT_CALL(read_callbacks, continueReading());
  filter->continueReading();
}

TEST_F(DynamicModuleNetworkFilterTest, WriteWithCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  Buffer::OwnedImpl data("test data");
  EXPECT_CALL(connection, write(testing::_, false));
  filter->write(data, false);
}

TEST_F(DynamicModuleNetworkFilterTest, CloseWithCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  EXPECT_CALL(connection, close(Network::ConnectionCloseType::NoFlush));
  filter->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(DynamicModuleNetworkFilterTest, ConnectionAccessor) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  // Verify connection() accessor works.
  EXPECT_EQ(&connection, &filter->connection());
}

TEST_F(DynamicModuleNetworkFilterTest, GetFilterConfig) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Verify getFilterConfig() returns the correct config.
  const auto& config = filter->getFilterConfig();
  EXPECT_NE(nullptr, config.in_module_config_);
}

TEST_F(DynamicModuleNetworkFilterTest, CallbackAccessors) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeInModuleFilter();

  // Before initialization, callbacks should be null.
  EXPECT_EQ(nullptr, filter->readCallbacks());
  EXPECT_EQ(nullptr, filter->writeCallbacks());

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));

  filter->initializeReadFilterCallbacks(read_callbacks);
  filter->initializeWriteFilterCallbacks(write_callbacks);

  // After initialization, callbacks should be set.
  EXPECT_EQ(&read_callbacks, filter->readCallbacks());
  EXPECT_EQ(&write_callbacks, filter->writeCallbacks());
}

TEST(DynamicModuleNetworkFilterConfigTest, ConfigInitialization) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "some_config", std::move(dynamic_module.value()), cluster_manager);
  EXPECT_TRUE(filter_config_or_status.ok());

  auto config = filter_config_or_status.value();
  EXPECT_NE(nullptr, config->in_module_config_);
  EXPECT_NE(nullptr, config->on_network_filter_config_destroy_);
  EXPECT_NE(nullptr, config->on_network_filter_new_);
  EXPECT_NE(nullptr, config->on_network_filter_new_connection_);
  EXPECT_NE(nullptr, config->on_network_filter_read_);
  EXPECT_NE(nullptr, config->on_network_filter_write_);
  EXPECT_NE(nullptr, config->on_network_filter_event_);
  EXPECT_NE(nullptr, config->on_network_filter_destroy_);
}

TEST(DynamicModuleNetworkFilterConfigTest, MissingSymbols) {
  // Use the HTTP-only no_op module which lacks network filter symbols.
  auto dynamic_module = newDynamicModule(testSharedObjectPath("no_op", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", std::move(dynamic_module.value()), cluster_manager);
  EXPECT_FALSE(filter_config_or_status.ok());
}

TEST(DynamicModuleNetworkFilterConfigTest, ConfigInitializationFailure) {
  // Use a module that returns nullptr from config_new.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_config_new_fail", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", std::move(dynamic_module.value()), cluster_manager);
  EXPECT_FALSE(filter_config_or_status.ok());
  EXPECT_THAT(filter_config_or_status.status().message(),
              testing::HasSubstr("Failed to initialize"));
}

TEST(DynamicModuleNetworkFilterConfigTest, StopIterationStatus) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_stop_iteration", "c"), false);
  EXPECT_TRUE(dynamic_module.ok()) << dynamic_module.status().message();

  NiceMock<Upstream::MockClusterManager> cluster_manager;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", std::move(dynamic_module.value()), cluster_manager);
  EXPECT_TRUE(filter_config_or_status.ok());
  auto config = filter_config_or_status.value();

  auto filter = std::make_shared<DynamicModuleNetworkFilter>(config);
  filter->initializeInModuleFilter();

  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));
  filter->initializeReadFilterCallbacks(read_callbacks);

  // All filter operations should return StopIteration.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onWrite(data, false));
}

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
