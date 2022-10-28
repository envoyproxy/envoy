#include <chrono>
#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);
constexpr int BUFFER_SIZE_BYTES = 0;
const std::string ZONE_NAME = "zone_name";
const std::string CLUSTER_NAME = "cluster_name";
const std::string NODE_NAME = "node_name";

// A helper test class to mock and intercept GrpcAccessLoggerImpl streams.
class GrpcAccessLoggerImplTestHelper {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks = Grpc::AsyncStreamCallbacks<
      opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse>;

  GrpcAccessLoggerImplTestHelper(LocalInfo::MockLocalInfo& local_info,
                                 Grpc::MockAsyncClient* async_client) {
    EXPECT_CALL(local_info, zoneName()).WillOnce(ReturnRef(ZONE_NAME));
    EXPECT_CALL(local_info, clusterName()).WillOnce(ReturnRef(CLUSTER_NAME));
    EXPECT_CALL(local_info, nodeName()).WillOnce(ReturnRef(NODE_NAME));
    EXPECT_CALL(*async_client, startRaw(_, _, _, _))
        .WillOnce(
            Invoke([this](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& cbs,
                          const Http::AsyncClient::StreamOptions&) {
              this->callbacks_ = dynamic_cast<AccessLogCallbacks*>(&cbs);
              return &this->stream_;
            }));
  }

  void expectStreamMessage(const std::string& expected_message_yaml) {
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(stream_, isAboveWriteBufferHighWatermark()).WillOnce(Return(false));
    EXPECT_CALL(stream_, sendMessageRaw_(_, false))
        .WillOnce(Invoke([expected_message](Buffer::InstancePtr& request, bool) {
          opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest message;
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
    *config_.mutable_common_config()->mutable_log_name() = "test_log_name";
    config_.mutable_common_config()->mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);
    config_.mutable_common_config()->mutable_buffer_flush_interval()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(FlushInterval).count());
    logger_ = std::make_unique<GrpcAccessLoggerImpl>(
        Grpc::RawAsyncClientPtr{async_client_}, config_, dispatcher_, local_info_, stats_store_);
  }

  Grpc::MockAsyncClient* async_client_;
  Stats::IsolatedStoreImpl stats_store_;
  LocalInfo::MockLocalInfo local_info_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  std::unique_ptr<GrpcAccessLoggerImpl> logger_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config_;
};

TEST_F(GrpcAccessLoggerImplTest, Log) {
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
  resource_logs:
    resource:
      attributes:
        - key: "log_name"
          value:
            string_value: "test_log_name"
        - key: "zone_name"
          value:
            string_value: "zone_name"
        - key: "cluster_name"
          value:
            string_value: "cluster_name"
        - key: "node_name"
          value:
            string_value: "node_name"
    scope_logs:
      - log_records:
          - severity_text: "test-severity-text"
  )EOF");
  opentelemetry::proto::logs::v1::LogRecord entry;
  entry.set_severity_text("test-severity-text");
  logger_->log(opentelemetry::proto::logs::v1::LogRecord(entry));
  // TCP logging shouldn't do anything.
  logger_->log(ProtobufWkt::Empty());
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
  LocalInfo::MockLocalInfo local_info_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  GrpcAccessLoggerCacheImpl logger_cache_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
};

// Test that the logger is created according to the config (by inspecting the generated log).
TEST_F(GrpcAccessLoggerCacheImplTest, LoggerCreation) {
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.mutable_common_config()->set_log_name("test-log");
  config.mutable_common_config()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  // Force a flush for every log entry.
  config.mutable_common_config()->mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);

  GrpcAccessLoggerSharedPtr logger =
      logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP);
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
  resource_logs:
    resource:
      attributes:
        - key: "log_name"
          value:
            string_value: "test-log"
        - key: "zone_name"
          value:
            string_value: "zone_name"
        - key: "cluster_name"
          value:
            string_value: "cluster_name"
        - key: "node_name"
          value:
            string_value: "node_name"
    scope_logs:
      - log_records:
          - severity_text: "test-severity-text"
  )EOF");
  opentelemetry::proto::logs::v1::LogRecord entry;
  entry.set_severity_text("test-severity-text");
  logger->log(opentelemetry::proto::logs::v1::LogRecord(entry));
}

TEST_F(GrpcAccessLoggerCacheImplTest, LoggerCreationResourceAttributes) {
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.mutable_common_config()->set_log_name("test_log");
  config.mutable_common_config()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  // Force a flush for every log entry.
  config.mutable_common_config()->mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);

  opentelemetry::proto::common::v1::KeyValueList keyValueList;
  const auto kv_yaml = R"EOF(
values:
- key: host_name
  value:
    string_value: test_host_name
- key: k8s.pod.uid
  value:
    string_value: xxxx-xxxx-xxxx-xxxx
- key: k8s.pod.createtimestamp
  value:
     int_value: 1655429509
  )EOF";
  TestUtility::loadFromYaml(kv_yaml, keyValueList);
  *config.mutable_resource_attributes() = keyValueList;

  GrpcAccessLoggerSharedPtr logger =
      logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP);
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
  resource_logs:
    resource:
      attributes:
        - key: "log_name"
          value:
            string_value: "test_log"
        - key: "zone_name"
          value:
            string_value: "zone_name"
        - key: "cluster_name"
          value:
            string_value: "cluster_name"
        - key: "node_name"
          value:
            string_value: "node_name"
        - key: "host_name"
          value:
            string_value: "test_host_name"
        - key: k8s.pod.uid
          value:
            string_value: xxxx-xxxx-xxxx-xxxx
        - key: k8s.pod.createtimestamp
          value:
            int_value: 1655429509
    scope_logs:
      - log_records:
          - severity_text: "test-severity-text"
  )EOF");
  opentelemetry::proto::logs::v1::LogRecord entry;
  entry.set_severity_text("test-severity-text");
  logger->log(opentelemetry::proto::logs::v1::LogRecord(entry));
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
