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
#include "test/test_common/utility.h"

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

const Protobuf::MethodDescriptor& mockMethodDescriptor() {
  // The mock logger doesn't have its own API, but we only care about the method descriptor so we
  // use the ALS protos.
  return *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      "envoy.service.accesslog.v3.AccessLogService.StreamAccessLogs");
}

// We don't care about the actual log entries, as this logger just adds them to the proto, but we
// need to use a proto type because the ByteSizeLong() is used to determine the log size, so we use
// standard Struct and Empty protos.
class MockGrpcAccessLoggerImpl
    : public Common::GrpcAccessLogger<Protobuf::Struct, Protobuf::Empty, Protobuf::Struct,
                                      Protobuf::Struct>,
      public Grpc::AsyncRequestCallbacks<Protobuf::Struct> {
public:
  MockGrpcAccessLoggerImpl(
      const Grpc::RawAsyncClientSharedPtr& client,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
      Event::Dispatcher& dispatcher, Stats::Scope& scope, std::string access_log_prefix,
      const Protobuf::MethodDescriptor& service_method, bool stream)
      : GrpcAccessLogger(config, dispatcher, scope, access_log_prefix,
                         createGrpcAccessLoggClient(stream, client, service_method, config)) {}

  std::unique_ptr<Common::GrpcAccessLogClient<Protobuf::Struct, Protobuf::Struct>>
  createGrpcAccessLoggClient(
      bool stream, const Grpc::RawAsyncClientSharedPtr& client,
      const Protobuf::MethodDescriptor& service_method,
      const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config) {
    if (stream) {
      return std::make_unique<
          Common::StreamingGrpcAccessLogClient<Protobuf::Struct, Protobuf::Struct>>(
          client, service_method, GrpcCommon::optionalRetryPolicy(config));
    }
    return std::make_unique<Common::UnaryGrpcAccessLogClient<Protobuf::Struct, Protobuf::Struct>>(
        client, service_method, GrpcCommon::optionalRetryPolicy(config),
        [this]() -> Grpc::AsyncRequestCallbacks<Protobuf::Struct>& { return *this; });
  }

  int numInits() const { return num_inits_; }

  int numClears() const { return num_clears_; }

  void onSuccess(Grpc::ResponsePtr<Protobuf::Struct>&&, Tracing::Span&) override {}
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}

  void onFailure(Grpc::Status::GrpcStatus, const std::string&, Tracing::Span&) override {}

  uint32_t countLogEntries() const override {
    uint32_t count = 0;
    for (const auto& field : message_.fields()) {
      count += static_cast<uint32_t>(field.second.number_value());
    }
    return count;
  }

private:
  void mockAddEntry(const std::string& key) {
    if (!message_.fields().contains(key)) {
      Protobuf::Value default_value;
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
  void addEntry(Protobuf::Struct&& entry) override {
    (void)entry;
    mockAddEntry(MOCK_HTTP_LOG_FIELD_NAME);
  }

  void addEntry(Protobuf::Empty&& entry) override {
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

class StreamingGrpcAccessLogTest : public testing::Test {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks = Grpc::AsyncStreamCallbacks<Protobuf::Struct>;

  // We log a non empty entry (even though not used) so that we can trigger buffering mechanisms,
  // which are based on the entry size.
  Protobuf::Struct mockHttpEntry() {
    Protobuf::Struct entry;
    entry.mutable_fields()->insert({"test-key", Protobuf::Value()});
    return entry;
  }

  void initLogger(std::chrono::milliseconds buffer_flush_interval_msec, size_t buffer_size_bytes) {
    timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(buffer_flush_interval_msec, _));
    config_.mutable_buffer_size_bytes()->set_value(buffer_size_bytes);
    config_.mutable_buffer_flush_interval()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(buffer_flush_interval_msec).count());

    logger_ = std::make_unique<MockGrpcAccessLoggerImpl>(
        Grpc::RawAsyncClientPtr{async_client_}, config_, dispatcher_, *stats_store_.rootScope(),
        "mock_access_log_prefix.", mockMethodDescriptor(), true);
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
          Protobuf::Struct message;
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
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config_;
};

// Test basic stream logging flow.
TEST_F(StreamingGrpcAccessLogTest, BasicFlow) {
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
  logger_->log(Protobuf::Empty());
  EXPECT_EQ(2, logger_->numClears());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Verify that sending an empty response message doesn't do anything bad.
  callbacks->onReceiveMessage(std::make_unique<Protobuf::Struct>());

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
  EXPECT_EQ(3,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
}

TEST_F(StreamingGrpcAccessLogTest, WatermarksOverrun) {
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

  // Entry wasn't sent but the message is still cleared
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Stream is still above high watermark, so this log entry will also be dropped.
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(true));
  EXPECT_CALL(stream, sendMessageRaw_(_, _)).Times(0);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  // Entry wasn't sent but the message is still cleared
  EXPECT_EQ(2, logger_->numClears());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Now allow the flush to happen. The stored log will get logged, and the next log will succeed.
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
  EXPECT_CALL(stream, sendMessageRaw_(_, _));
  logger_->log(mockHttpEntry());

  // This is the third clear - one for each log entry attempt (even the dropped ones).
  EXPECT_EQ(3, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test the canLogMore function with zero buffer size
TEST_F(StreamingGrpcAccessLogTest, CanLogMoreWithZeroBufferSize) {
  // Set buffer size to 0, which should always allow logging
  initLogger(FlushInterval, 0);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // With buffer size 0, canLogMore should always return true
  // and logs should be sent immediately
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test canLogMore behavior with non-zero buffer size
TEST_F(StreamingGrpcAccessLogTest, CanLogMoreWithNonZeroBufferSize) {
  // Set a small buffer size to test buffer management
  const int buffer_size = 10;
  initLogger(FlushInterval, buffer_size);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // First log should succeed and be under buffer size
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Now simulate a case where the buffer would be exceeded but flush succeeds
  // This tests the path where canLogMore flushes and then returns true
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillRepeatedly(Return(false));
  EXPECT_CALL(stream, sendMessageRaw_(_, false)).WillRepeatedly(Return());

  // Create an entry larger than the buffer size
  Protobuf::Struct big_entry = mockHttpEntry();
  const std::string big_key(buffer_size * 2, 'a');
  big_entry.mutable_fields()->insert({big_key, Protobuf::Value()});

  logger_->log(std::move(big_entry));
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
}

// Test TCP logging functionality
TEST_F(StreamingGrpcAccessLogTest, TcpLogging) {
  // Use the same approach as the BasicFlow test which is known to work
  initLogger(FlushInterval, 0);

  // Start a stream for the first log
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // First log an HTTP entry to establish the stream
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Now log a TCP entry and verify it increments the logs_written counter
  expectFlushedLogEntriesCount(stream, MOCK_TCP_LOG_FIELD_NAME, 1);
  logger_->log(Protobuf::Empty());
  EXPECT_EQ(2, logger_->numClears());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test TCP logging with timer-based flush
TEST_F(StreamingGrpcAccessLogTest, TcpLoggingWithTimerFlush) {
  // Use a large buffer size so flush happens due to timer, not buffer size
  const int buffer_size = 1000000;
  initLogger(FlushInterval, buffer_size);

  // Start a stream for the logs
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // First log an HTTP entry to establish the stream
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());

  // Simulate the stream being established by calling onCreateInitialMetadata
  Http::TestRequestHeaderMapImpl empty_headers;
  callbacks->onCreateInitialMetadata(empty_headers);

  EXPECT_EQ(1, logger_->numInits());
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Now log some TCP entries - they won't be flushed yet due to large buffer size
  logger_->log(Protobuf::Empty());
  logger_->log(Protobuf::Empty());
  logger_->log(Protobuf::Empty());
  logger_->log(Protobuf::Empty());

  // Set up expectations for the flush that will happen when timer fires
  expectFlushedLogEntriesCount(stream, MOCK_TCP_LOG_FIELD_NAME, 4);

  // Trigger flush via timer
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();

  // Verify logs_written counter is incremented for each log (1 HTTP + 4 TCP)
  EXPECT_EQ(5,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test the logs_written stats counter is incremented with multiple entries
TEST_F(StreamingGrpcAccessLogTest, LogsWrittenStatsMultipleEntries) {
  initLogger(FlushInterval, 0);

  // Start a stream for the logs
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // Set up a custom handler for the flush to verify multiple entries
  // Use WillRepeatedly since both methods will be called for each log entry
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillRepeatedly(Return(false));
  EXPECT_CALL(stream, sendMessageRaw_(_, false))
      .WillRepeatedly(Invoke([](Buffer::InstancePtr& request, bool) {
        // We don't need to verify the exact count in the message
        // We just need to make sure the message is sent successfully
        Protobuf::Struct message;
        Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
        EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
      }));

  // Log multiple entries at once to test batch stats counting
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());

  // Verify that the logs_written counter is incremented by the number of entries
  EXPECT_EQ(3,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test the logs_dropped stats counter is incremented with multiple entries
TEST_F(StreamingGrpcAccessLogTest, LogsDroppedStatsMultipleEntries) {
  InSequence s;
  initLogger(FlushInterval, 1);

  // Start a stream for the logs
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // Simulate multiple entries being dropped due to stream being above watermark
  // Use WillRepeatedly since isAboveWriteBufferHighWatermark() will be called for each log entry
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillRepeatedly(Return(true));
  EXPECT_CALL(stream, sendMessageRaw_(_, _)).Times(0);

  // Create a batch of entries that will be dropped
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());

  // Verify that the logs_dropped counter is incremented by the number of entries
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(3,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test the mixed success and failure cases for stats counters
TEST_F(StreamingGrpcAccessLogTest, MixedSuccessAndFailureStats) {
  InSequence s;
  initLogger(FlushInterval, 0);

  // Start a stream for the logs
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // First log entry succeeds
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Next, simulate stream failure
  callbacks->onRemoteClose(Grpc::Status::Internal, "connection failure");

  // Set up a new stream that will fail
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            callbacks.onRemoteClose(Grpc::Status::Unavailable, "unavailable");
            return nullptr;
          }));

  // Log entry should be dropped due to stream failure
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Set up a new successful stream
  expectStreamStart(stream, &callbacks);
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);

  // Log entry should succeed
  logger_->log(mockHttpEntry());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test that stream failure is handled correctly.
TEST_F(StreamingGrpcAccessLogTest, StreamFailure) {
  initLogger(FlushInterval, 0);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions& options) {
            EXPECT_FALSE(options.retry_policy.has_value());
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  // When stream connection fails, logs should be counted as dropped
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

TEST_F(StreamingGrpcAccessLogTest, StreamFailureAndRetry) {
  config_.mutable_grpc_stream_retry_policy()->mutable_num_retries()->set_value(2);
  config_.mutable_grpc_stream_retry_policy()
      ->mutable_retry_back_off()
      ->mutable_base_interval()
      ->set_seconds(1);
  initLogger(FlushInterval, 1);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks&,
                    const Http::AsyncClient::StreamOptions& options) -> Grpc::RawAsyncStream* {
            EXPECT_TRUE(options.retry_policy.has_value());
            EXPECT_TRUE(options.retry_policy.value().has_num_retries());
            EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(options.retry_policy.value(), num_retries), 2);
            return nullptr;
          }));
  logger_->log(mockHttpEntry());
  // When stream connection fails during retry, logs should be counted as dropped
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test that logs are dropped when stream connection fails after being established
TEST_F(StreamingGrpcAccessLogTest, StreamConnectionFailureAfterEstablished) {
  initLogger(FlushInterval, 0);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // Log an HTTP entry.
  expectFlushedLogEntriesCount(stream, MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  EXPECT_EQ(1, logger_->numInits());
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Close the stream to simulate connection failure
  callbacks->onRemoteClose(Grpc::Status::Internal, "connection failure");

  // Attempt to log again, but this time the stream will fail to send
  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            // Simulate immediate stream failure
            callbacks.onRemoteClose(Grpc::Status::Unavailable, "unavailable");
            return nullptr;
          }));

  logger_->log(mockHttpEntry());

  // The second log should be counted as dropped
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test that log entries are batched.
TEST_F(StreamingGrpcAccessLogTest, Batching) {
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
  Protobuf::Struct big_entry = mockHttpEntry();
  const std::string big_key(max_buffer_size, 'a');
  big_entry.mutable_fields()->insert({big_key, Protobuf::Value()});
  logger_->log(std::move(big_entry));
  EXPECT_EQ(2, logger_->numClears());
}

// Test that log entries are flushed periodically.
TEST_F(StreamingGrpcAccessLogTest, Flushing) {
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
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());

  // Flush on empty message does nothing.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();
}

class UnaryGrpcAccessLogTest : public testing::Test {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks = Grpc::AsyncRequestCallbacks<Protobuf::Struct>;

  // We log a non empty entry (even though not used) so that we can trigger buffering mechanisms,
  // which are based on the entry size.
  Protobuf::Struct mockHttpEntry() {
    Protobuf::Struct entry;
    entry.mutable_fields()->insert({"test-key", Protobuf::Value()});
    return entry;
  }

  void initLogger(std::chrono::milliseconds buffer_flush_interval_msec, size_t buffer_size_bytes) {
    timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(buffer_flush_interval_msec, _));
    config_.mutable_buffer_size_bytes()->set_value(buffer_size_bytes);
    config_.mutable_buffer_flush_interval()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(buffer_flush_interval_msec).count());

    logger_ = std::make_unique<MockGrpcAccessLoggerImpl>(
        Grpc::RawAsyncClientPtr{async_client_}, config_, dispatcher_, *stats_store_.rootScope(),
        "mock_access_log_prefix.", mockMethodDescriptor(), false);
  }

  void expectFlushedLogEntriesCount(const std::string& key, int count) {
    EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillOnce(
            Invoke([key, count](absl::string_view, absl::string_view, Buffer::InstancePtr&& request,
                                Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                                const Http::AsyncClient::RequestOptions&) {
              Protobuf::Struct message;
              Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
              EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
              EXPECT_TRUE(message.fields().contains(key));
              EXPECT_EQ(message.fields().at(key).number_value(), count);
              return nullptr; // We don't care about the returned request.
            }));
  }

  Stats::IsolatedStoreImpl stats_store_;
  Event::MockTimer* timer_ = nullptr;
  Event::MockDispatcher dispatcher_;
  Grpc::MockAsyncClient* async_client_{new Grpc::MockAsyncClient};
  std::unique_ptr<MockGrpcAccessLoggerImpl> logger_;
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config_;
};

// Test basic stream logging flow.
TEST_F(UnaryGrpcAccessLogTest, BasicFlow) {
  initLogger(FlushInterval, 0);
  // Log an HTTP entry.
  expectFlushedLogEntriesCount(MOCK_HTTP_LOG_FIELD_NAME, 1);
  logger_->log(mockHttpEntry());
  // Message should be initialized and cleared every time a request is sent.
  EXPECT_EQ(1, logger_->numInits());
  EXPECT_EQ(1, logger_->numClears());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());

  // Log a TCP entry.
  expectFlushedLogEntriesCount(MOCK_TCP_LOG_FIELD_NAME, 1);
  logger_->log(Protobuf::Empty());
  // Message should be initialized and cleared every time a request is sent.
  EXPECT_EQ(2, logger_->numInits());
  EXPECT_EQ(2, logger_->numClears());
  EXPECT_EQ(2,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  // No dropped logs expected.
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

TEST_F(UnaryGrpcAccessLogTest, FailureAndRetry) {
  config_.mutable_grpc_stream_retry_policy()->mutable_num_retries()->set_value(2);
  config_.mutable_grpc_stream_retry_policy()
      ->mutable_retry_back_off()
      ->mutable_base_interval()
      ->set_seconds(1);
  initLogger(FlushInterval, 1);
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
      .WillOnce(Invoke([](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                          Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                          const Http::AsyncClient::RequestOptions& options) -> Grpc::AsyncRequest* {
        EXPECT_TRUE(options.retry_policy.has_value());
        EXPECT_TRUE(options.retry_policy.value().has_num_retries());
        EXPECT_EQ(PROTOBUF_GET_WRAPPED_REQUIRED(options.retry_policy.value(), num_retries), 2);
        return nullptr;
      }));
  logger_->log(mockHttpEntry());

  // For unary client, logs are always considered written regardless of the outcome
  // This is because the unary client always returns true from log() method
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test that unary client handles request failure correctly
TEST_F(UnaryGrpcAccessLogTest, RequestFailure) {
  initLogger(FlushInterval, 0);

  // Mock a failed request
  Grpc::MockAsyncRequest request;
  EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
      .WillOnce(Invoke([&request](absl::string_view, absl::string_view, Buffer::InstancePtr&&,
                                  Grpc::RawAsyncRequestCallbacks& callbacks, Tracing::Span& span,
                                  const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
        // Simulate failure by calling onFailure
        callbacks.onFailure(Grpc::Status::Internal, "internal error", span);
        return &request;
      }));

  logger_->log(mockHttpEntry());

  // For unary client, logs are always considered written regardless of the outcome
  // This is because the unary client always returns true from log() method
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

// Test that log entries are batched.
TEST_F(UnaryGrpcAccessLogTest, Batching) {
  // The approximate log size for buffering is calculated based on each entry's byte size.
  const int max_buffer_size = 3 * mockHttpEntry().ByteSizeLong();
  initLogger(FlushInterval, max_buffer_size);

  expectFlushedLogEntriesCount(MOCK_HTTP_LOG_FIELD_NAME, 3);
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());
  logger_->log(mockHttpEntry());
  // The entries were batched and logged together so we expect a single init and clear.
  EXPECT_EQ(1, logger_->numInits());
  EXPECT_EQ(1, logger_->numClears());

  // Logging an entry that's bigger than the buffer size should trigger another flush.
  expectFlushedLogEntriesCount(MOCK_HTTP_LOG_FIELD_NAME, 1);
  Protobuf::Struct big_entry = mockHttpEntry();
  const std::string big_key(max_buffer_size, 'a');
  big_entry.mutable_fields()->insert({big_key, Protobuf::Value()});
  logger_->log(std::move(big_entry));
  EXPECT_EQ(2, logger_->numClears());
}

// Test that log entries are flushed periodically.
TEST_F(UnaryGrpcAccessLogTest, Flushing) {
  initLogger(FlushInterval, 100);

  // Nothing to do yet.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();

  // Not enough data yet to trigger flush on batch size.
  logger_->log(mockHttpEntry());

  expectFlushedLogEntriesCount(MOCK_HTTP_LOG_FIELD_NAME, 1);
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
               Event::Dispatcher& dispatcher) override {
    auto client = THROW_OR_RETURN_VALUE(
        async_client_manager_.factoryForGrpcService(config.grpc_service(), scope_, true)
            .value()
            ->createUncachedRawAsyncClient(),
        Grpc::RawAsyncClientPtr);
    return std::make_shared<MockGrpcAccessLoggerImpl>(std::move(client), config, dispatcher, scope_,
                                                      "mock_access_log_prefix.",
                                                      mockMethodDescriptor(), true);
  }
};

class GrpcAccessLoggerCacheTest : public testing::Test {
public:
  GrpcAccessLoggerCacheTest() : logger_cache_(async_client_manager_, *scope_.rootScope(), tls_) {}

  void expectClientCreation() {
    factory_ = new Grpc::MockAsyncClientFactory;
    async_client_ = new Grpc::MockAsyncClient;
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, true))
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
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  MockGrpcAccessLoggerCache logger_cache_;
};

TEST_F(GrpcAccessLoggerCacheTest, Deduplication) {
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.set_log_name("log-1");
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-1");

  expectClientCreation();
  MockGrpcAccessLoggerImpl::SharedPtr logger1 =
      logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP);
  EXPECT_EQ(logger1, logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP));

  // Do not deduplicate different types of logger
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::TCP));

  // Changing log name leads to another logger.
  config.set_log_name("log-2");
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP));

  config.set_log_name("log-1");
  EXPECT_EQ(logger1, logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP));

  // Changing cluster name leads to another logger.
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-2");
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP));
}

// Test the case where canLogMore returns false after a flush
TEST_F(StreamingGrpcAccessLogTest, CanLogMoreReturnsFalseAfterFlush) {
  // Set a small buffer size to test buffer management
  const int buffer_size = 10;
  initLogger(FlushInterval, buffer_size);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  // First, set up a scenario where the buffer is full and flush fails
  // This tests the path where canLogMore flushes and then returns false
  EXPECT_CALL(stream, isAboveWriteBufferHighWatermark()).WillRepeatedly(Return(true));
  EXPECT_CALL(stream, sendMessageRaw_(_, _)).Times(0);

  // Create an entry larger than the buffer size
  Protobuf::Struct big_entry = mockHttpEntry();
  const std::string big_key(buffer_size * 2, 'a');
  big_entry.mutable_fields()->insert({big_key, Protobuf::Value()});

  // This should try to flush but fail, resulting in a dropped log
  logger_->log(std::move(big_entry));

  // Verify the log was dropped
  EXPECT_EQ(0,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_written")->value());
  EXPECT_EQ(1,
            TestUtility::findCounter(stats_store_, "mock_access_log_prefix.logs_dropped")->value());
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
