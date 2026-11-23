#include <functional>

#include "source/common/http/message_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/stream_id_provider_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/filters/network/dynamic_modules/filter.h"

#include "test/extensions/dynamic_modules/util.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/thread_local_cluster.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace NetworkFilters {

using ::Envoy::StatusHelpers::HasStatusMessage;
using ::Envoy::StatusHelpers::IsOk;
using ::testing::Not;

class DynamicModuleNetworkFilterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  void SetUp() override {
    auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
    EXPECT_OK(dynamic_module);

    auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
        "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
        cluster_manager_, *stats_.rootScope(), main_thread_dispatcher_);
    EXPECT_OK(filter_config_or_status);
    filter_config_ = filter_config_or_status.value();
    // Re-open stat creation so tests can call `define_*` from the test thread.
    filter_config_->stat_creation_frozen_ = false;

    ON_CALL(connection_, dispatcher()).WillByDefault(testing::ReturnRef(worker_thread_dispatcher_));
    ON_CALL(read_callbacks_, connection()).WillByDefault(testing::ReturnRef(connection_));
    ON_CALL(read_callbacks_, readBuffer())
        .WillByDefault(testing::Return(makeOptRef<Buffer::Instance>(connection_read_buffer_)));
  }

  // Runs the reentrancy probe with an inline callout completion and returns how many times the
  // module observed on_http_callout_done. The gate must keep this at zero for inline completions.
  uint64_t runInlineCalloutProbe(
      std::function<void(Http::AsyncClient::Callbacks&, Http::AsyncClient::Request&)> complete) {
    auto dynamic_module =
        newDynamicModule(testSharedObjectPath("network_integration_test", "rust"), false);
    EXPECT_OK(dynamic_module);
    auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
        "http_callout_reentrancy_probe", "", DefaultMetricsNamespace,
        std::move(dynamic_module.value()), cluster_manager_, *stats_.rootScope(),
        main_thread_dispatcher_);
    EXPECT_OK(filter_config_or_status);

    auto cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
    EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{"callout_cluster"}))
        .WillOnce(testing::Return(cluster.get()));
    EXPECT_CALL(cluster->async_client_, send_(testing::_, testing::_, testing::_))
        .WillOnce([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                      const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
          testing::NiceMock<Http::MockAsyncClientRequest> req{&cluster->async_client_};
          complete(callbacks, req);
          return nullptr;
        });

    auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_or_status.value());
    filter->initializeReadFilterCallbacks(read_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

    auto counter = TestUtility::findCounter(stats_, "dynamicmodulescustom.callout_done_total");
    EXPECT_NE(counter, nullptr);
    return counter == nullptr ? 0 : counter->value();
  }

  Stats::IsolatedStoreImpl stats_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  DynamicModuleNetworkFilterConfigSharedPtr filter_config_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Event::MockDispatcher> worker_thread_dispatcher_{"worker_0"};
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  Buffer::OwnedImpl connection_read_buffer_;
};

TEST_F(DynamicModuleNetworkFilterTest, BasicDataFlow) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);

  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks;

  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->initializeWriteFilterCallbacks(write_callbacks);

  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  Buffer::OwnedImpl read_data("hello");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(read_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(read_data, true));

  Buffer::OwnedImpl write_data("world");
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onWrite(write_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onWrite(write_data, true));

  // The read buffer is the connection buffer and persists for access from on_scheduled and other
  // callbacks. The write buffer is only valid during on_write, so it is cleared once the call
  // returns.
  EXPECT_NE(nullptr, filter->currentReadBuffer());
  EXPECT_EQ(nullptr, filter->currentWriteBuffer());
}

TEST_F(DynamicModuleNetworkFilterTest, DeferredReadBufferResolvesConnectionBuffer) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  // injectReadDataToFilterChain delivers on_read a transient buffer that is freed when the
  // injecting frame returns. Scope it so it is gone before the deferred access below.
  {
    Buffer::OwnedImpl transient("transient");
    EXPECT_EQ(Network::FilterStatus::Continue, filter->onData(transient, false));
  }

  // Outside on_read, deferred access resolves the connection read buffer, never the freed transient
  // one.
  EXPECT_EQ(&connection_read_buffer_, filter->currentReadBuffer());
}

TEST_F(DynamicModuleNetworkFilterTest, AllConnectionEvents) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);

  filter->initializeReadFilterCallbacks(read_callbacks_);

  // Test all connection events.
  filter->onEvent(Network::ConnectionEvent::Connected);
  filter->onEvent(Network::ConnectionEvent::RemoteClose);
  filter->onEvent(Network::ConnectionEvent::LocalClose);
  filter->onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
}

TEST_F(DynamicModuleNetworkFilterTest, WatermarkCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  // These should not crash.
  filter->onAboveWriteBufferHighWatermark();
  filter->onBelowWriteBufferLowWatermark();
}

TEST_F(DynamicModuleNetworkFilterTest, FilterDestroyWithIsDestroyedCheck) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

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
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_filter_new_fail", "c"), false);
  EXPECT_OK(dynamic_module);

  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager_, *stats_.rootScope(), main_thread_dispatcher_);
  EXPECT_OK(filter_config_or_status);
  auto filter_config = filter_config_or_status.value();
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  // These should return StopIteration and close connection when in_module_filter_ is null.
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
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
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  // Without initializing callbacks, continueReading should not crash.
  filter->continueReading();
}

TEST_F(DynamicModuleNetworkFilterTest, WriteNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  // Without initializing callbacks, write should not crash.
  Buffer::OwnedImpl data("test");
  filter->write(data, false);
}

TEST_F(DynamicModuleNetworkFilterTest, CloseNullCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  // Without initializing callbacks, close should not crash.
  filter->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(DynamicModuleNetworkFilterTest, ContinueReadingWithCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(read_callbacks_, continueReading());
  filter->continueReading();
}

TEST_F(DynamicModuleNetworkFilterTest, WriteWithCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  Buffer::OwnedImpl data("test data");
  EXPECT_CALL(connection_, write(testing::_, false));
  filter->write(data, false);
}

TEST_F(DynamicModuleNetworkFilterTest, CloseWithCallbacks) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  filter->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(DynamicModuleNetworkFilterTest, ConnectionAccessor) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  // Verify connection() accessor works.
  EXPECT_EQ(&connection_, &filter->connection());
}

TEST_F(DynamicModuleNetworkFilterTest, GetFilterConfig) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  // Verify getFilterConfig() returns the correct config.
  const auto& config = filter->getFilterConfig();
  EXPECT_NE(nullptr, config.in_module_config_);
}

TEST_F(DynamicModuleNetworkFilterTest, CallbackAccessors) {
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);

  // Before initialization, callbacks should be null.
  EXPECT_EQ(nullptr, filter->readCallbacks());
  EXPECT_EQ(nullptr, filter->writeCallbacks());

  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks;

  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->initializeWriteFilterCallbacks(write_callbacks);

  // After initialization, callbacks should be set.
  EXPECT_EQ(&read_callbacks_, filter->readCallbacks());
  EXPECT_EQ(&write_callbacks, filter->writeCallbacks());
}

TEST(DynamicModuleNetworkFilterConfigTest, ConfigInitialization) {
  auto dynamic_module = newDynamicModule(testSharedObjectPath("network_no_op", "c"), false);
  EXPECT_OK(dynamic_module);

  Stats::IsolatedStoreImpl stats;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "some_config", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager, *stats.rootScope(), main_thread_dispatcher);
  EXPECT_OK(filter_config_or_status);

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
  EXPECT_OK(dynamic_module);

  Stats::IsolatedStoreImpl stats;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager, *stats.rootScope(), main_thread_dispatcher);
  EXPECT_THAT(filter_config_or_status, Not(IsOk()));
}

TEST(DynamicModuleNetworkFilterConfigTest, ConfigInitializationFailure) {
  // Use a module that returns nullptr from config_new.
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_config_new_fail", "c"), false);
  EXPECT_OK(dynamic_module);

  Stats::IsolatedStoreImpl stats;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager, *stats.rootScope(), main_thread_dispatcher);
  EXPECT_THAT(filter_config_or_status,
              HasStatusMessage(testing::HasSubstr("Failed to initialize")));
}

TEST(DynamicModuleNetworkFilterConfigTest, StopIterationStatus) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_stop_iteration", "c"), false);
  EXPECT_OK(dynamic_module);

  Stats::IsolatedStoreImpl stats;
  NiceMock<Upstream::MockClusterManager> cluster_manager;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher;
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager, *stats.rootScope(), main_thread_dispatcher);
  EXPECT_OK(filter_config_or_status);
  auto config = filter_config_or_status.value();

  NiceMock<Event::MockDispatcher> worker_thread_dispatcher{"worker_0"};
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, dispatcher()).WillByDefault(testing::ReturnRef(worker_thread_dispatcher));
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));

  auto filter = std::make_shared<DynamicModuleNetworkFilter>(config);
  filter->initializeReadFilterCallbacks(read_callbacks);

  // All filter operations should return StopIteration.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());

  Buffer::OwnedImpl data("test");
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onData(data, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onWrite(data, false));
}

// -----------------------------------------------------------------------------
// Worker index tests
// -----------------------------------------------------------------------------

// The filter publishes the worker index parsed from its dispatcher name. Malformed names are
// covered by the shared helper unit test.
TEST_F(DynamicModuleNetworkFilterTest, WorkerIndexParsedFromDispatcherName) {
  NiceMock<Event::MockDispatcher> worker_dispatcher{"worker_7"};
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks;
  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, dispatcher()).WillByDefault(testing::ReturnRef(worker_dispatcher));
  ON_CALL(read_callbacks, connection()).WillByDefault(testing::ReturnRef(connection));

  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks);
  EXPECT_EQ(7U, filter->workerIndex());
}

// -----------------------------------------------------------------------------
// Access Log Tests
// -----------------------------------------------------------------------------

// The connection logs through the filter, which forwards each record to every configured log.
TEST_F(DynamicModuleNetworkFilterTest, AccessLogForwardsToConfiguredLogs) {
  auto first_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  auto second_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config_->access_logs_ = {first_log, second_log};
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);

  const Formatter::Context log_context{
      nullptr, nullptr, nullptr, {}, AccessLog::AccessLogType::TcpConnectionEnd};
  EXPECT_CALL(*first_log, log(testing::Ref(log_context), testing::Ref(connection_.stream_info_)));
  EXPECT_CALL(*second_log, log(testing::Ref(log_context), testing::Ref(connection_.stream_info_)));
  filter->log(log_context, connection_.stream_info_);
}

// A new connection gets a stream ID for its records, and no flush timer without an interval.
TEST_F(DynamicModuleNetworkFilterTest, AccessLogsSetStreamIdOnNewConnection) {
  filter_config_->access_logs_ = {std::make_shared<testing::StrictMock<AccessLog::MockInstance>>()};
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config_->random_generator_ = random;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(worker_thread_dispatcher_, createTimer_(testing::_)).Times(0);
  EXPECT_CALL(connection_.stream_info_, setStreamIdProvider(testing::_))
      .WillOnce([&random](StreamInfo::StreamIdProviderSharedPtr provider) {
        EXPECT_EQ(random.uuid_, provider->toStringView().value());
      });
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
}

// A stream ID that the connection already has, such as one from an earlier filter, is kept.
TEST_F(DynamicModuleNetworkFilterTest, AccessLogsKeepExistingStreamId) {
  filter_config_->access_logs_ = {std::make_shared<testing::StrictMock<AccessLog::MockInstance>>()};
  testing::StrictMock<Random::MockRandomGenerator> random;
  filter_config_->random_generator_ = random;
  const StreamInfo::StreamIdProviderImpl existing_stream_id("existing-stream-id");
  ON_CALL(connection_.stream_info_, getStreamIdProvider())
      .WillByDefault(
          testing::Return(makeOptRef<const StreamInfo::StreamIdProvider>(existing_stream_id)));
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(connection_.stream_info_, setStreamIdProvider(testing::_)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
}

// Without access logs the filter leaves the connection as it was, even with a flush interval.
TEST_F(DynamicModuleNetworkFilterTest, NoAccessLogStateWithoutAccessLogs) {
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(worker_thread_dispatcher_, createTimer_(testing::_)).Times(0);
  EXPECT_CALL(connection_.stream_info_, setStreamIdProvider(testing::_)).Times(0);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  filter->onEvent(Network::ConnectionEvent::RemoteClose);
  EXPECT_FALSE(connection_.stream_info_.downstream_timing_.downstreamConnectionEnd().has_value());
}

// A connection that is closing or closed gets no stream ID, start record or flush timer.
TEST_F(DynamicModuleNetworkFilterTest, NoAccessLogStateOnClosedConnection) {
  filter_config_->access_logs_ = {std::make_shared<testing::StrictMock<AccessLog::MockInstance>>()};
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  filter_config_->flush_access_log_on_start_ = true;
  testing::StrictMock<Random::MockRandomGenerator> random;
  filter_config_->random_generator_ = random;
  EXPECT_CALL(worker_thread_dispatcher_, createTimer_(testing::_)).Times(0);
  EXPECT_CALL(connection_.stream_info_, setStreamIdProvider(testing::_)).Times(0);
  for (const Network::Connection::State state :
       {Network::Connection::State::Closing, Network::Connection::State::Closed}) {
    SCOPED_TRACE(static_cast<int>(state));
    connection_.state_ = state;
    auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
    filter->initializeReadFilterCallbacks(read_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
  }
}

// A start flush logs the new connection after it gets its stream ID and before the module can
// report on it.
TEST_F(DynamicModuleNetworkFilterTest, FlushAccessLogOnStart) {
  auto access_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config_->access_logs_ = {access_log};
  filter_config_->flush_access_log_on_start_ = true;
  testing::StrictMock<Random::MockRandomGenerator> random;
  filter_config_->random_generator_ = random;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  testing::InSequence s;
  EXPECT_CALL(random, uuid());
  EXPECT_CALL(connection_.stream_info_, setStreamIdProvider(testing::_));
  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([&filter](const Formatter::Context& log_context, const StreamInfo::StreamInfo&) {
        EXPECT_EQ(AccessLog::AccessLogType::TcpConnectionStart, log_context.accessLogType());
        EXPECT_FALSE(envoy_dynamic_module_callback_network_filter_add_custom_flag(filter.get(),
                                                                                  {"early", 5}));
      });
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());
}

// The flush timer logs a periodic record and then snapshots the bytes meters that exist.
TEST_F(DynamicModuleNetworkFilterTest, PeriodicAccessLog) {
  auto access_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config_->access_logs_ = {access_log};
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config_->random_generator_ = random;
  auto* timer = new NiceMock<Event::MockTimer>(&worker_thread_dispatcher_);
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(100), testing::_)).Times(3);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  // Without bytes meters the flush only logs.
  connection_.stream_info_.upstream_bytes_meter_ = nullptr;
  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([](const Formatter::Context& log_context, const StreamInfo::StreamInfo&) {
        EXPECT_EQ(AccessLog::AccessLogType::TcpPeriodic, log_context.accessLogType());
      });
  timer->invokeCallback();

  auto downstream_bytes_meter = std::make_shared<StreamInfo::BytesMeter>();
  auto upstream_bytes_meter = std::make_shared<StreamInfo::BytesMeter>();
  connection_.stream_info_.downstream_bytes_meter_ = downstream_bytes_meter;
  connection_.stream_info_.upstream_bytes_meter_ = upstream_bytes_meter;
  downstream_bytes_meter->addWireBytesReceived(10);
  upstream_bytes_meter->addWireBytesSent(20);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([&](const Formatter::Context& log_context, const StreamInfo::StreamInfo&) {
        EXPECT_EQ(AccessLog::AccessLogType::TcpPeriodic, log_context.accessLogType());
        EXPECT_EQ(nullptr, downstream_bytes_meter->bytesAtLastDownstreamPeriodicLog());
        EXPECT_EQ(nullptr, upstream_bytes_meter->bytesAtLastDownstreamPeriodicLog());
      });
  timer->invokeCallback();
  const StreamInfo::BytesMeter::BytesSnapshot* downstream_snapshot =
      downstream_bytes_meter->bytesAtLastDownstreamPeriodicLog();
  ASSERT_NE(nullptr, downstream_snapshot);
  EXPECT_EQ(10, downstream_snapshot->wire_bytes_received);
  EXPECT_EQ(simTime().systemTime(), downstream_snapshot->snapshot_time);
  const StreamInfo::BytesMeter::BytesSnapshot* upstream_snapshot =
      upstream_bytes_meter->bytesAtLastDownstreamPeriodicLog();
  ASSERT_NE(nullptr, upstream_snapshot);
  EXPECT_EQ(20, upstream_snapshot->wire_bytes_sent);
  EXPECT_EQ(simTime().systemTime(), upstream_snapshot->snapshot_time);
}

// A connected event, such as the end of a downstream TLS handshake, keeps the flush timer and does
// not end the connection.
TEST_F(DynamicModuleNetworkFilterTest, ConnectedEventsKeepAccessLogState) {
  filter_config_->access_logs_ = {std::make_shared<testing::StrictMock<AccessLog::MockInstance>>()};
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config_->random_generator_ = random;
  auto* timer = new NiceMock<Event::MockTimer>(&worker_thread_dispatcher_);
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  filter->onEvent(Network::ConnectionEvent::Connected);
  filter->onEvent(Network::ConnectionEvent::ConnectedZeroRtt);
  EXPECT_FALSE(timer_destroyed);
  EXPECT_FALSE(connection_.stream_info_.downstream_timing_.downstreamConnectionEnd().has_value());
}

// Either close records when the connection ended and destroys the flush timer.
TEST_F(DynamicModuleNetworkFilterTest, CloseRecordsConnectionEndAndStopsPeriodicAccessLogs) {
  filter_config_->access_logs_ = {std::make_shared<testing::StrictMock<AccessLog::MockInstance>>()};
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid()).Times(2);
  filter_config_->random_generator_ = random;
  for (const Network::ConnectionEvent event :
       {Network::ConnectionEvent::RemoteClose, Network::ConnectionEvent::LocalClose}) {
    SCOPED_TRACE(static_cast<int>(event));
    connection_.stream_info_.downstream_timing_ = {};
    auto* timer = new NiceMock<Event::MockTimer>(&worker_thread_dispatcher_);
    bool timer_destroyed = false;
    timer->timer_destroyed_ = &timer_destroyed;
    auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
    filter->initializeReadFilterCallbacks(read_callbacks_);
    EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

    simTime().advanceTimeWait(std::chrono::seconds(1));
    filter->onEvent(event);
    EXPECT_TRUE(timer_destroyed);
    EXPECT_EQ(simTime().monotonicTime(),
              connection_.stream_info_.downstream_timing_.downstreamConnectionEnd());
  }
}

// Without a module filter the connection still starts its access logs before the filter closes it,
// and the close records the end and destroys the flush timer.
TEST_F(DynamicModuleNetworkFilterTest, AccessLogsWithoutInModuleFilter) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_filter_new_fail", "c"), false);
  EXPECT_OK(dynamic_module);
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "test_filter", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager_, *stats_.rootScope(), main_thread_dispatcher_);
  EXPECT_OK(filter_config_or_status);
  auto filter_config = filter_config_or_status.value();
  auto access_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config->access_logs_ = {access_log};
  filter_config->access_log_flush_interval_ = std::chrono::milliseconds(100);
  filter_config->flush_access_log_on_start_ = true;
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config->random_generator_ = random;
  auto* timer = new NiceMock<Event::MockTimer>(&worker_thread_dispatcher_);
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(connection_.stream_info_, setStreamIdProvider(testing::_));
  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([](const Formatter::Context& log_context, const StreamInfo::StreamInfo&) {
        EXPECT_EQ(AccessLog::AccessLogType::TcpConnectionStart, log_context.accessLogType());
      });
  // The mock connection raises the close event when the filter closes it.
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());
  EXPECT_TRUE(timer_destroyed);
  EXPECT_EQ(simTime().monotonicTime(),
            connection_.stream_info_.downstream_timing_.downstreamConnectionEnd());
}

// An access logger that closes the connection during a periodic flush destroys the flush timer,
// which the flush then leaves alone.
TEST_F(DynamicModuleNetworkFilterTest, ConnectionClosedDuringPeriodicAccessLog) {
  auto access_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config_->access_logs_ = {access_log};
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config_->random_generator_ = random;
  auto* timer = new NiceMock<Event::MockTimer>(&worker_thread_dispatcher_);
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([this](const Formatter::Context&, const StreamInfo::StreamInfo&) {
        connection_.close(Network::ConnectionCloseType::NoFlush);
      });
  timer->invokeCallback();
  EXPECT_TRUE(timer_destroyed);
}

// An access logger that closes the connection while the filter writes the start record destroys
// the flush timer, and the module does not see the new connection after the close.
TEST_F(DynamicModuleNetworkFilterTest, ConnectionClosedDuringStartAccessLog) {
  auto access_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config_->access_logs_ = {access_log};
  filter_config_->access_log_flush_interval_ = std::chrono::milliseconds(100);
  filter_config_->flush_access_log_on_start_ = true;
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config_->random_generator_ = random;
  auto* timer = new NiceMock<Event::MockTimer>(&worker_thread_dispatcher_);
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([this](const Formatter::Context&, const StreamInfo::StreamInfo&) {
        // The mock connection is closed when it raises the close event.
        connection_.close(Network::ConnectionCloseType::NoFlush);
      });
  // The network_no_op module would continue the new connection.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());
  EXPECT_TRUE(timer_destroyed);
  // The module never got the connection, so it cannot report on it.
  EXPECT_FALSE(
      envoy_dynamic_module_callback_network_filter_add_custom_flag(filter.get(), {"late", 4}));
}

// An access logger that starts a flushed close during the start record also keeps the module from
// seeing the new connection.
TEST_F(DynamicModuleNetworkFilterTest, ConnectionClosingDuringStartAccessLog) {
  auto access_log = std::make_shared<testing::StrictMock<AccessLog::MockInstance>>();
  filter_config_->access_logs_ = {access_log};
  filter_config_->flush_access_log_on_start_ = true;
  testing::StrictMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, uuid());
  filter_config_->random_generator_ = random;
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);

  EXPECT_CALL(*access_log, log(testing::_, testing::Ref(connection_.stream_info_)))
      .WillOnce([this](const Formatter::Context&, const StreamInfo::StreamInfo&) {
        // A flushed close that waits for pending data raises no close event yet.
        connection_.state_ = Network::Connection::State::Closing;
      });
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter->onNewConnection());
}

// -----------------------------------------------------------------------------
// Metrics Tests
// -----------------------------------------------------------------------------

TEST_F(DynamicModuleNetworkFilterTest, DefineAndIncrementCounter) {
  // Define a counter on the config.
  size_t counter_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"test_counter", 12};
  auto result = envoy_dynamic_module_callback_network_filter_config_define_counter(
      filter_config_.get(), name, &counter_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(counter_id, 1);

  // Create filter and increment counter.
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  result =
      envoy_dynamic_module_callback_network_filter_increment_counter(filter.get(), counter_id, 5);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify counter value.
  auto counter = filter_config_->metrics().getCounterById(counter_id);
  EXPECT_TRUE(counter.has_value());
}

TEST_F(DynamicModuleNetworkFilterTest, DefineAndManipulateGauge) {
  // Define a gauge on the config.
  size_t gauge_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"test_gauge", 10};
  auto result = envoy_dynamic_module_callback_network_filter_config_define_gauge(
      filter_config_.get(), name, &gauge_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(gauge_id, 1);

  // Create filter and manipulate gauge.
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  result = envoy_dynamic_module_callback_network_filter_set_gauge(filter.get(), gauge_id, 100);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  result = envoy_dynamic_module_callback_network_filter_increment_gauge(filter.get(), gauge_id, 50);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  result = envoy_dynamic_module_callback_network_filter_decrement_gauge(filter.get(), gauge_id, 25);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify gauge exists.
  auto gauge = filter_config_->metrics().getGaugeById(gauge_id);
  EXPECT_TRUE(gauge.has_value());
}

TEST_F(DynamicModuleNetworkFilterTest, DefineAndRecordHistogram) {
  // Define a histogram on the config.
  size_t histogram_id = 0;
  envoy_dynamic_module_type_module_buffer name = {"test_histogram", 14};
  auto result = envoy_dynamic_module_callback_network_filter_config_define_histogram(
      filter_config_.get(), name, &histogram_id);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);
  EXPECT_EQ(histogram_id, 1);

  // Create filter and record histogram value.
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  result = envoy_dynamic_module_callback_network_filter_record_histogram_value(filter.get(),
                                                                               histogram_id, 42);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_Success);

  // Verify histogram exists.
  auto histogram = filter_config_->metrics().getHistogramById(histogram_id);
  EXPECT_TRUE(histogram.has_value());
}

TEST_F(DynamicModuleNetworkFilterTest, MetricNotFound) {
  // Create filter without defining metrics.
  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_);
  filter->initializeReadFilterCallbacks(read_callbacks_);
  filter->setCallbacksForTest(nullptr);

  // Try to use invalid metric IDs.
  auto result =
      envoy_dynamic_module_callback_network_filter_increment_counter(filter.get(), 999, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);

  result = envoy_dynamic_module_callback_network_filter_set_gauge(filter.get(), 999, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);

  result = envoy_dynamic_module_callback_network_filter_increment_gauge(filter.get(), 999, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);

  result = envoy_dynamic_module_callback_network_filter_decrement_gauge(filter.get(), 999, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);

  result =
      envoy_dynamic_module_callback_network_filter_record_histogram_value(filter.get(), 999, 1);
  EXPECT_EQ(result, envoy_dynamic_module_type_metrics_result_MetricNotFound);
}

// The async client can complete a callout inline, before send returns a request handle, while the
// module is still inside the hook that issued the callout. The filter must not call back into the
// module in that window. An inline reset exercises the onFailure gate.
TEST_F(DynamicModuleNetworkFilterTest, HttpCalloutInlineFailureDoesNotReenterModule) {
  EXPECT_EQ(0, runInlineCalloutProbe(
                   [](Http::AsyncClient::Callbacks& callbacks, Http::AsyncClient::Request& req) {
                     callbacks.onFailure(req, Http::AsyncClient::FailureReason::Reset);
                   }));
}

// An inline response, such as a local reply from a cluster with no healthy hosts, exercises the
// onSuccess gate on the same inline window.
TEST_F(DynamicModuleNetworkFilterTest, HttpCalloutInlineSuccessDoesNotReenterModule) {
  EXPECT_EQ(
      0, runInlineCalloutProbe([](Http::AsyncClient::Callbacks& callbacks,
                                  Http::AsyncClient::Request& req) {
        Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(
            Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
        callbacks.onSuccess(req, std::move(response));
      }));
}

class DynamicModuleNetworkFilterLanguageTest : public DynamicModuleNetworkFilterTest,
                                               public testing::WithParamInterface<std::string> {};

#ifndef __SANITIZE_ADDRESS__
auto DynamicModuleNetworkFilterLanguageTestValues = testing::Values("rust", "go", "cpp");
#else
auto DynamicModuleNetworkFilterLanguageTestValues = testing::Values("rust", "go");
#endif

INSTANTIATE_TEST_SUITE_P(Languages, DynamicModuleNetworkFilterLanguageTest,
                         DynamicModuleNetworkFilterLanguageTestValues,
                         DynamicModuleTestLanguages::languageParamToTestName);

// Each SDK sets a namespace from a serialized google.protobuf.Struct and a typed namespace from a
// serialized google.protobuf.Any on new connection.
TEST_P(DynamicModuleNetworkFilterLanguageTest, DynamicMetadata) {
  auto dynamic_module =
      newDynamicModule(testSharedObjectPath("network_integration_test", GetParam()), false);
  EXPECT_OK(dynamic_module);
  auto filter_config_or_status = newDynamicModuleNetworkFilterConfig(
      "dynamic_metadata", "", DefaultMetricsNamespace, std::move(dynamic_module.value()),
      cluster_manager_, *stats_.rootScope(), main_thread_dispatcher_);
  EXPECT_OK(filter_config_or_status);

  auto filter = std::make_shared<DynamicModuleNetworkFilter>(filter_config_or_status.value());
  filter->initializeReadFilterCallbacks(read_callbacks_);
  EXPECT_EQ(Network::FilterStatus::Continue, filter->onNewConnection());

  const auto& metadata = connection_.stream_info_.metadata_;
  ASSERT_TRUE(metadata.filter_metadata().contains("dynamic_modules.struct"));
  EXPECT_EQ(
      "v", metadata.filter_metadata().at("dynamic_modules.struct").fields().at("k").string_value());
  ASSERT_TRUE(metadata.typed_filter_metadata().contains("dynamic_modules.typed"));
  const auto& typed_metadata = metadata.typed_filter_metadata().at("dynamic_modules.typed");
  EXPECT_EQ("t/x", typed_metadata.type_url());
  EXPECT_EQ(std::string("\x01\x02", 2), typed_metadata.value());
}

} // namespace NetworkFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
