#include <chrono>
#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"
#include "envoy/service/accesslog/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/extensions/access_loggers/grpc/http_grpc_access_log_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);
constexpr int BUFFER_SIZE_BYTES = 0;

// A helper test class to mock and intercept GrpcAccessLoggerImpl streams.
class GrpcAccessLoggerImplTestHelper {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks =
      Grpc::AsyncStreamCallbacks<envoy::service::accesslog::v3::StreamAccessLogsResponse>;

  GrpcAccessLoggerImplTestHelper(LocalInfo::MockLocalInfo& local_info,
                                 Grpc::MockAsyncClient* async_client) {
    EXPECT_CALL(local_info, node());
    EXPECT_CALL(*async_client, startRaw(_, _, _, _))
        .WillOnce(
            Invoke([this](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& cbs,
                          const Http::AsyncClient::StreamOptions&) {
              this->callbacks_ = dynamic_cast<AccessLogCallbacks*>(&cbs);
              return &this->stream_;
            }));
  }

  void expectStreamMessage(const std::string& expected_message_yaml) {
    envoy::service::accesslog::v3::StreamAccessLogsMessage expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(stream_, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
    EXPECT_CALL(stream_, sendMessageRaw_(_, false))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          envoy::service::accesslog::v3::StreamAccessLogsMessage message;
          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
        }));
  }

private:
  MockAccessLogStream stream_;
  AccessLogCallbacks* callbacks_;
};

class GrpcAccessLoggerImplTest : public testing::Test {
public:
  GrpcAccessLoggerImplTest()
      : async_client_(new Grpc::MockAsyncClient), timer_(new Event::MockTimer(&dispatcher_)),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_) {
    EXPECT_CALL(*timer_, enableTimer(_, _));
    *config_.mutable_log_name() = "test_log_name";
    config_.mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);
    config_.mutable_buffer_flush_interval()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(FlushInterval).count());
    logger_ =
        std::make_unique<GrpcAccessLoggerImpl>(Grpc::RawAsyncClientPtr{async_client_}, config_,
                                               dispatcher_, local_info_, *stats_store_.rootScope());
  }

  Grpc::MockAsyncClient* async_client_;
  Stats::IsolatedStoreImpl stats_store_;
  LocalInfo::MockLocalInfo local_info_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  std::unique_ptr<GrpcAccessLoggerImpl> logger_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config_;
};

TEST_F(GrpcAccessLoggerImplTest, LogHttp) {
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
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
}

TEST_F(GrpcAccessLoggerImplTest, LogTcp) {
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
identifier:
  node:
    id: node_name
    cluster: cluster_name
    locality:
      zone: zone_name
  log_name: test_log_name
tcp_logs:
  log_entry:
    common_properties:
      sample_rate: 1.0
)EOF");
  envoy::data::accesslog::v3::TCPAccessLogEntry tcp_entry;
  tcp_entry.mutable_common_properties()->set_sample_rate(1);
  logger_->log(envoy::data::accesslog::v3::TCPAccessLogEntry(tcp_entry));
}

class GrpcAccessLoggerCacheImplTest : public testing::Test {
public:
  GrpcAccessLoggerCacheImplTest()
      : async_client_(new Grpc::MockAsyncClient), factory_(new Grpc::MockAsyncClientFactory),
        logger_cache_(async_client_manager_, scope_, tls_, local_info_),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_) {
    EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, true))
        .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool) {
          EXPECT_CALL(*factory_, createUncachedRawAsyncClient()).WillOnce(Invoke([this] {
            return Grpc::RawAsyncClientPtr{async_client_};
          }));
          return Grpc::AsyncClientFactoryPtr{factory_};
        }));
  }

  Grpc::MockAsyncClient* async_client_;
  Grpc::MockAsyncClientFactory* factory_;
  Grpc::MockAsyncClientManager async_client_manager_;
  NiceMock<Stats::MockIsolatedStatsStore> store_;
  Stats::Scope& scope_{*store_.rootScope()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  LocalInfo::MockLocalInfo local_info_;
  GrpcAccessLoggerCacheImpl logger_cache_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
};

// Test that the logger is created according to the config (by inspecting the generated log).
TEST_F(GrpcAccessLoggerCacheImplTest, LoggerCreation) {
  envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
  config.set_log_name("test-log");
  config.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
  // Force a flush for every log entry.
  config.mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);

  GrpcAccessLoggerSharedPtr logger =
      logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP);
  // Note that the local info node() method is mocked, so the node is not really configurable.
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
  identifier:
    node:
      id: node_name
      cluster: cluster_name
      locality:
        zone: zone_name
    log_name: test-log
  http_logs:
    log_entry:
      request:
        path: /test/path1
  )EOF");
  envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
  entry.mutable_request()->set_path("/test/path1");
  logger->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
}

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
