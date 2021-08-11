#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/grpc_access_logger.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);
constexpr char MOCK_HTTP_LOG_FIELD_NAME[] = "http_log_entry";
constexpr char MOCK_TCP_LOG_FIELD_NAME[] = "tcp_log_entry";
constexpr auto TRANSPORT_API_VERSION = envoy::config::core::v3::ApiVersion::AUTO;

const Protobuf::MethodDescriptor& mockMethodDescriptor() {
  // The mock logger doesn't have its own API, but we only care about the method descriptor so we
  // use the ALS protos.
  return Grpc::VersionedMethods("envoy.service.accesslog.v3.AccessLogService.StreamAccessLogs",
                                "envoy.service.accesslog.v2.AccessLogService.StreamAccessLogs")
      .getMethodDescriptorForVersion(TRANSPORT_API_VERSION);
}

// We don't care about the actual log entries, as this logger just adds them to the proto, but we
// need to use a proto type because the ByteSizeLong() is used to determine the log size, so we use
// standard Struct and Empty protos.
class MockGrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<ProtobufWkt::Struct, ProtobufWkt::Empty, ProtobufWkt::Struct,
                                      ProtobufWkt::Struct> {
public:
  MockGrpcAccessLoggerImpl(const Grpc::RawAsyncClientSharedPtr& client,
                           std::chrono::milliseconds buffer_flush_interval_msec,
                           uint64_t max_buffer_size_bytes, Event::Dispatcher& dispatcher,
                           Stats::Scope& scope, std::string access_log_prefix,
                           const Protobuf::MethodDescriptor& service_method,
                           envoy::config::core::v3::ApiVersion transport_api_version)
      : GrpcAccessLogger(std::move(client), buffer_flush_interval_msec, max_buffer_size_bytes,
                         dispatcher, scope, access_log_prefix, service_method,
                         transport_api_version) {}

  int numInits() const { return num_inits_; }

  int numClears() const { return num_clears_; }

private:
  void mockAddEntry(const std::string& key) {
    if (!message_.fields().contains(key)) {
      ProtobufWkt::Value default_value;
      default_value.set_number_value(0);
      message_.mutable_fields()->insert({key, default_value});
    }
    message_.mutable_fields()->at(key).set_number_value(message_.fields().at(key).number_value() +
                                                        1);
  }

  // Extensions::AccessLoggers::GrpcCommon::GrpcAccessLogger
  // For testing purposes, we don't really care how each of these virtual methods is implemented, as
  // it's up to each logger implementation. We test whether they were called in the regular flow of
  // logging or not. For example, we count how many entries were added, but don't add the log entry
  // itself to the message.
  void addEntry(ProtobufWkt::Struct&& entry) override {
    (void)entry;
    mockAddEntry(MOCK_HTTP_LOG_FIELD_NAME);
  }

  void addEntry(ProtobufWkt::Empty&& entry) override {
    (void)entry;
    mockAddEntry(MOCK_TCP_LOG_FIELD_NAME);
  }

  bool isEmpty() override { return message_.fields().empty(); }

  void initMessage() override { ++num_inits_; }

  void clearMessage() override {
    message_.Clear();
    num_clears_++;
  }

  int num_inits_ = 0;
  int num_clears_ = 0;
};

class GrpcAccessLogTest : public testing::Test {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks = Grpc::AsyncStreamCallbacks<ProtobufWkt::Struct>;

  // We log a non empty entry (even though not used) so that we can trigger buffering mechanisms,
  // which are based on the entry size.
  ProtobufWkt::Struct mockHttpEntry() {
    ProtobufWkt::Struct entry;
    entry.mutable_fields()->insert({"test-key", ProtobufWkt::Value()});
    return entry;
  }

  void initLogger(std::chrono::milliseconds buffer_flush_interval_msec, size_t buffer_size_bytes) {
    timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(buffer_flush_interval_msec, _));
    logger_ = std::make_unique<MockGrpcAccessLoggerImpl>(
        Grpc::RawAsyncClientPtr{async_client_}, buffer_flush_interval_msec, buffer_size_bytes,
        dispatcher_, stats_store_, "mock_access_log_prefix.", mockMethodDescriptor(),
        TRANSPORT_API_VERSION);
  }

  void expectStreamStart(MockAccessLogStream& stream, AccessLogCallbacks** callbacks_to_set) {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
        .WillOnce(Invoke([&stream, callbacks_to_set](absl::string_view, absl::string_view,
                                                     Grpc::RawAsyncStreamCallbacks& callbacks,
                                                     const Http::AsyncClient::StreamOptions&) {
          *callbacks_to_set = dynamic_cast<AccessLogCallbacks*>(&callbacks);
          return &stream;
        }));
  }

  void expectFlushedLogEntriesCount(MockAccessLogStream& stream, const std::string& key,
                                    int count) {
    EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
    EXPECT_CALL(stream, sendMessageRaw_(_, false))
        .WillOnce(Invoke([key, count](Buffer::InstancePtr& request, bool) {
          ProtobufWkt::Struct message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_TRUE(message.fields().contains(key));
          EXPECT_EQ(message.fields().at(key).number_value(), count);
        }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  Event::MockTimer* timer_ = nullptr;
  Event::MockDispatcher dispatcher_;
  Grpc::MockAsyncClient* async_client_{new Grpc::MockAsyncClient};
  std::unique_ptr<MockGrpcAccessLoggerImpl> logger_;
};

// Test basic stream logging flow.
TEST_F(GrpcAccessLogTest, BasicFlow) {
  initLogger(FlushInterval, 0);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  // Log an HTTP entry.
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  // Messages should be cleared after each flush.
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Log a TCP entry.
  expectFlushedLogEntriesCount(stream, MOCK_TCP_LOG_FIELD_NAME, 1);
  logger_->log(ProtobufWkt::Empty());
  EXPECT_EQ(2, logger_->numClears());
  // TCP logging doesn't change the logs_written counter.
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Verify that sending an empty response message doesn't do anything bad.
  callbacks->onReceiveMessage(std::make_unique<ProtobufWkt::Struct>());

  // Close the stream and make sure we make a new one.
  callbacks->onRemoteClose(Grpc::Status::Internal, "bad");

  expectStreamStart(stream, &callbacks);
  // Log an HTTP entry.
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  // Message should be initialized again.
  EXPECT_EQ(2, logger_->numInits());
  EXPECT_EQ(3, logger_->numClears());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
}

TEST_F(GrpcAccessLogTest, WatermarksOverrun) {
  InSequence s;
  initLogger(FlushInterval, 1);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // Fail to flush, so the log stays buffered up.
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(stream, sendMessageRaw_(_, false)).Times(0);
  logger_->log(mockHttpEntry());
  // No entry was logged so no clear expected.
  EXPECT_EQ(0, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Now canLogMore will fail, and the next log will be dropped.
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(stream, sendMessageRaw_(_, _)).Times(0);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  // Still no entry was logged so no clear expected.
  EXPECT_EQ(0, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Now allow the flush to happen. The stored log will get logged, and the next log will
  // succeed.
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  EXPECT_CALL(stream, sendMessageRaw_(_, _));
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  EXPECT_CALL(stream, sendMessageRaw_(_, _));
  logger_->log(mockHttpEntry());
  // Now both entries were logged separately so we expect 2 clears.
  EXPECT_EQ(2, logger_->numClears());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test that stream failure is handled correctly.
TEST_F(GrpcAccessLogTest, StreamFailure) {
  initLogger(FlushInterval, 0);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
}

// Test that log entries are batched.
TEST_F(GrpcAccessLogTest, Batching) {
  // The approximate log size for buffering is calculated based on each entry's byte size.
  const int max_buffer_size = 3 * mockHttpEntry().ByteSizeLong();
  initLogger(FlushInterval, max_buffer_size);

  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 3);
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  // The entries were batched and logged together so we expect a single clear.
  EXPECT_EQ(1, logger_->numClears());

  // Logging an entry that's bigger than the buffer size should trigger another flush.
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  ProtobufWkt::Struct big_entry = mockHttpEntry();
  const std::string big_key(max_buffer_size, 'a');
  big_entry.mutable_fields()->insert({big_key, ProtobufWkt::Value()});
  logger_->log(std::move(big_entry));
  EXPECT_EQ(2, logger_->numClears());
}

// Test that log entries are flushed periodically.
TEST_F(GrpcAccessLogTest, Flushing) {
  initLogger(FlushInterval, 100);

  // Nothing to do yet.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();

  // Not enough data yet to trigger flush on batch size.
  logger_->log(mockHttpEntry());

  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();
  EXPECT_EQ(1, logger_->numInits());

  // Flush on empty message does nothing.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();
}

class MockGrpcAccessLoggerCache
    : public Common::GrpcAccessLoggerCache<
          MockGrpcAccessLoggerImpl,
          envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig> {
public:
  MockGrpcAccessLoggerCache(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                            ThreadLocal::SlotAllocator& tls)
      : GrpcAccessLoggerCache(async_client_manager, scope, tls) {}

private:
  // Common::GrpcAccessLoggerCache
  MockGrpcAccessLoggerImpl::SharedPtr
  createLogger(const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
               envoy::config::core::v3::ApiVersion, const Grpc::RawAsyncClientSharedPtr& client,
               std::chrono::milliseconds buffer_flush_interval_msec, uint64_t max_buffer_size_bytes,
               Event::Dispatcher& dispatcher, Stats::Scope& scope) override {
    return std::make_shared<MockGrpcAccessLoggerImpl>(
        std::move(client), buffer_flush_interval_msec, max_buffer_size_bytes, dispatcher, scope,
        "mock_access_log_prefix.", mockMethodDescriptor(), config.transport_api_version());
  }
};

class GrpcAccessLoggerCacheTest : public testing::Test {
public:
  GrpcAccessLoggerCacheTest() : logger_cache_(async_client_manager_, scope_, tls_) {}

  void expectClientCreation() {
    factory_ = new Grpc::MockAsyncClientFactory;
    async_client_ = new Grpc::MockAsyncClient;
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, false))
        .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, createUncachedRawAsyncClient()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Grpc::MockAsyncClientManager async_client_manager_;
  Grpc::MockAsyncClient* async_client_ = nullptr;
  Grpc::MockAsyncClientFactory* factory_ = nullptr;
  MockGrpcAccessLoggerCache logger_cache_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

TEST_F(GrpcAccessLoggerCacheTest, Deduplication) {
  Stats::IsolatedStoreImpl scope;

  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.set_log_name("log-1");
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-1");

  expectClientCreation();
  MockGrpcAccessLoggerImpl::SharedPtr logger1 = logger_cache_.getOrCreateLogger(
      config, envoy::config::core::v3::ApiVersion::V3, Common::GrpcAccessLoggerType::HTTP, scope);
  EXPECT_EQ(logger1,
            logger_cache_.getOrCreateLogger(config, envoy::config::core::v3::ApiVersion::V3,
                                            Common::GrpcAccessLoggerType::HTTP, scope));

  // Do not deduplicate different types of logger
  expectClientCreation();
  EXPECT_NE(logger1,
            logger_cache_.getOrCreateLogger(config, envoy::config::core::v3::ApiVersion::V3,
                                            Common::GrpcAccessLoggerType::TCP, scope));

  // Changing log name leads to another logger.
  config.set_log_name("log-2");
  expectClientCreation();
  EXPECT_NE(logger1,
            logger_cache_.getOrCreateLogger(config, envoy::config::core::v3::ApiVersion::V3,
                                            Common::GrpcAccessLoggerType::HTTP, scope));

  config.set_log_name("log-1");
  EXPECT_EQ(logger1,
            logger_cache_.getOrCreateLogger(config, envoy::config::core::v3::ApiVersion::V3,
                                            Common::GrpcAccessLoggerType::HTTP, scope));

  // Changing cluster name leads to another logger.
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-2");
  expectClientCreation();
  EXPECT_NE(logger1,
            logger_cache_.getOrCreateLogger(config, envoy::config::core::v3::ApiVersion::V3,
                                            Common::GrpcAccessLoggerType::HTTP, scope));
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
