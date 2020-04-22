#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/network/address_impl.h"

#include "extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);

class GrpcAccessLoggerImplTest : public testing::Test {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks =
      Grpc::AsyncStreamCallbacks<envoy::service::accesslog::v3::StreamAccessLogsResponse>;

  void initLogger(std::chrono::milliseconds buffer_flush_interval_msec, size_t buffer_size_bytes) {
    timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*timer_, enableTimer(buffer_flush_interval_msec, _));
    logger_ = std::make_unique<GrpcAccessLoggerImpl>(Grpc::RawAsyncClientPtr{async_client_},
                                                     log_name_, buffer_flush_interval_msec,
                                                     buffer_size_bytes, dispatcher_, local_info_);
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

  void expectStreamMessage(MockAccessLogStream& stream, const std::string& expected_message_yaml) {
    envoy::service::accesslog::v3::StreamAccessLogsMessage expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(stream, sendMessageRaw_(_, false))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          envoy::service::accesslog::v3::StreamAccessLogsMessage message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
        }));
  }

  std::string log_name_ = "test_log_name";
  LocalInfo::MockLocalInfo local_info_;
  Event::MockTimer* timer_ = nullptr;
  Event::MockDispatcher dispatcher_;
  Grpc::MockAsyncClient* async_client_{new Grpc::MockAsyncClient};
  std::unique_ptr<GrpcAccessLoggerImpl> logger_;
};

// Test basic stream logging flow.
TEST_F(GrpcAccessLoggerImplTest, BasicFlow) {
  InSequence s;
  initLogger(FlushInterval, 0);

  // Start a stream for the first log.
  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  expectStreamMessage(stream, R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
    request:
      path: /test/path1
)EOF");
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path("/test/path1");
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));

  expectStreamMessage(stream, R"EOF(
http_logs:
  log_entry:
    request:
      path: /test/path2
)EOF");
  entry.mutable_request()->set_path("/test/path2");
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));

  // Verify that sending an empty response message doesn't do anything bad.
  callbacks->onReceiveMessage(
      std::make_unique<envoy::service::accesslog::v3::StreamAccessLogsResponse>());

  // Close the stream and make sure we make a new one.
  callbacks->onRemoteClose(Grpc::Status::Internal, "bad");
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  expectStreamMessage(stream, R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
    request:
      path: /test/path3
)EOF");
  entry.mutable_request()->set_path("/test/path3");
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

// Test that stream failure is handled correctly.
TEST_F(GrpcAccessLoggerImplTest, StreamFailure) {
  InSequence s;
  initLogger(FlushInterval, 0);

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  EXPECT_CALL(local_info_, node());
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

// Test that log entries are batched.
TEST_F(GrpcAccessLoggerImplTest, Batching) {
  InSequence s;
  initLogger(FlushInterval, 100);

  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  const std::string path1(30, '1');
  const std::string path2(30, '2');
  const std::string path3(80, '3');
  expectStreamMessage(stream, fmt::format(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
http_logs:
  log_entry:
  - request:
      path: "{}"
  - request:
      path: "{}"
  - request:
      path: "{}"
)EOF",
                                          path1, path2, path3));
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path(path1);
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
  entry.mutable_request()->set_path(path2);
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
  entry.mutable_request()->set_path(path3);
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));

  const std::string path4(120, '4');
  expectStreamMessage(stream, fmt::format(R"EOF(
http_logs:
  log_entry:
    request:
      path: "{}"
)EOF",
                                          path4));
  entry.mutable_request()->set_path(path4);
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

// Test that log entries are flushed periodically.
TEST_F(GrpcAccessLoggerImplTest, Flushing) {
  InSequence s;
  initLogger(FlushInterval, 100);

  // Nothing to do yet.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();

  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  // Not enough data yet to trigger flush on batch size.
  entry.mutable_request()->set_path("/test/path1");
  logger_->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));

  MockAccessLogStream stream;
  AccessLogCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);
  EXPECT_CALL(local_info_, node());
  expectStreamMessage(stream, fmt::format(R"EOF(
  identifier:
    node:
      id: node_name
      cluster: cluster_name
      locality:
        zone: zone_name
    log_name: test_log_name
  http_logs:
    log_entry:
    - request:
        path: /test/path1
  )EOF"));
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();

  // Flush on empty message does nothing.
  EXPECT_CALL(*timer_, enableTimer(FlushInterval, _));
  timer_->invokeCallback();
}

class GrpcAccessLoggerCacheImplTest : public testing::Test {
public:
  GrpcAccessLoggerCacheImplTest() {
    logger_cache_ = std::make_unique<GrpcAccessLoggerCacheImpl>(async_client_manager_, scope_, tls_,
                                                                local_info_);
  }

  void expectClientCreation() {
    factory_ = new Grpc::MockAsyncClientFactory;
    async_client_ = new Grpc::MockAsyncClient;
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, false))
        .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  LocalInfo::MockLocalInfo local_info_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Grpc::MockAsyncClientManager async_client_manager_;
  Grpc::MockAsyncClient* async_client_ = nullptr;
  Grpc::MockAsyncClientFactory* factory_ = nullptr;
  std::unique_ptr<GrpcAccessLoggerCacheImpl> logger_cache_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
};

TEST_F(GrpcAccessLoggerCacheImplTest, Deduplication) {
  InSequence s;

  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.set_log_name("log-1");
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-1");

  expectClientCreation();
  GrpcAccessLoggerSharedPtr logger1 =
      logger_cache_->getOrCreateLogger(config, GrpcAccessLoggerType::HTTP);
  EXPECT_EQ(logger1, logger_cache_->getOrCreateLogger(config, GrpcAccessLoggerType::HTTP));

  // Do not deduplicate different types of logger
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_->getOrCreateLogger(config, GrpcAccessLoggerType::TCP));

  // Changing log name leads to another logger.
  config.set_log_name("log-2");
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_->getOrCreateLogger(config, GrpcAccessLoggerType::HTTP));

  config.set_log_name("log-1");
  EXPECT_EQ(logger1, logger_cache_->getOrCreateLogger(config, GrpcAccessLoggerType::HTTP));

  // Changing cluster name leads to another logger.
  config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("cluster-2");
  expectClientCreation();
  EXPECT_NE(logger1, logger_cache_->getOrCreateLogger(config, GrpcAccessLoggerType::HTTP));
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
