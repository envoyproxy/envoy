#include <chrono>
#include <memory>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/common.h"
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

// A helper test class to mock and intercept GrpcAccessLoggerImpl requests.
class GrpcAccessLoggerImplTestHelper {
public:
  GrpcAccessLoggerImplTestHelper(LocalInfo::MockLocalInfo& local_info,
                                 Grpc::MockAsyncClient* async_client, bool expect_call = true)
      : async_client_(async_client) {
    if (expect_call) {
      EXPECT_CALL(local_info, zoneName()).WillOnce(ReturnRef(ZONE_NAME));
      EXPECT_CALL(local_info, clusterName()).WillOnce(ReturnRef(CLUSTER_NAME));
      EXPECT_CALL(local_info, nodeName()).WillOnce(ReturnRef(NODE_NAME));
    }
  }

  void expectSentMessage(
      const std::string& expected_message_yaml,
      Grpc::Status::GrpcStatus response_status = Grpc::Status::WellKnownGrpcStatus::Ok,
      uint32_t rejected_log_records = 0) {
    opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest expected_message;
    TestUtility::loadFromYaml(expected_message_yaml, expected_message);
    EXPECT_CALL(*async_client_, sendRaw(_, _, _, _, _, _))
        .WillOnce(Invoke([expected_message, response_status, rejected_log_records](
                             absl::string_view, absl::string_view, Buffer::InstancePtr&& request,
                             Grpc::RawAsyncRequestCallbacks& callback, Tracing::Span& span,
                             const Http::AsyncClient::RequestOptions&) {
          opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest message;

          Buffer::ZeroCopyInputStreamImpl request_stream(std::move(request));
          EXPECT_TRUE(message.ParseFromZeroCopyStream(&request_stream));
          EXPECT_EQ(message.DebugString(), expected_message.DebugString());
          if (response_status != Grpc::Status::WellKnownGrpcStatus::Ok) {
            callback.onFailure(response_status, "err", span);
          } else {
            opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse resp;
            resp.mutable_partial_success()->set_rejected_log_records(rejected_log_records);
            callback.onSuccessRaw(Grpc::Common::serializeMessage(resp), span);
          }
          return nullptr; // We don't care about the returned request.
        }));
  }

private:
  Grpc::MockAsyncClient* async_client_;
};

class GrpcAccessLoggerImplTest : public testing::Test {
public:
  GrpcAccessLoggerImplTest()
      : async_client_(new Grpc::MockAsyncClient), timer_(new Event::MockTimer(&dispatcher_)),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_, true) {
    EXPECT_CALL(*timer_, enableTimer(_, _));
    *config_.mutable_common_config()->mutable_log_name() = "test_log_name";
    config_.mutable_common_config()->mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);
    config_.mutable_common_config()->mutable_buffer_flush_interval()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(FlushInterval).count());
  }

  Grpc::MockAsyncClient* async_client_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  LocalInfo::MockLocalInfo local_info_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  std::unique_ptr<GrpcAccessLoggerImpl> logger_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config_;

  void setUpLogger() {
    logger_ =
        std::make_unique<GrpcAccessLoggerImpl>(Grpc::RawAsyncClientPtr{async_client_}, config_,
                                               dispatcher_, local_info_, *stats_store_.rootScope());
  }
};

TEST_F(GrpcAccessLoggerImplTest, Log) {
  setUpLogger();
  grpc_access_logger_impl_test_helper_.expectSentMessage(R"EOF(
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
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
  // TCP logging shouldn't do anything.
  logger_->log(ProtobufWkt::Empty());
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
}

TEST_F(GrpcAccessLoggerImplTest, LogWithStats) {
  setUpLogger();
  std::string expected_message_yaml = R"EOF(
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
  )EOF";
  grpc_access_logger_impl_test_helper_.expectSentMessage(expected_message_yaml);
  opentelemetry::proto::logs::v1::LogRecord entry;
  entry.set_severity_text("test-severity-text");
  logger_->log(opentelemetry::proto::logs::v1::LogRecord(entry));
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_dropped")
                .value()
                .get()
                .value(),
            0);

  grpc_access_logger_impl_test_helper_.expectSentMessage(
      expected_message_yaml, Grpc::Status::WellKnownGrpcStatus::Internal, 1);
  logger_->log(opentelemetry::proto::logs::v1::LogRecord(entry));
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_dropped")
                .value()
                .get()
                .value(),
            1);
}

TEST_F(GrpcAccessLoggerImplTest, StatsWithCustomPrefix) {
  *config_.mutable_stat_prefix() = "custom.";
  setUpLogger();
  grpc_access_logger_impl_test_helper_.expectSentMessage(R"EOF(
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
  EXPECT_EQ(
      stats_store_.findCounterByString("access_logs.open_telemetry_access_log.custom.logs_written")
          .value()
          .get()
          .value(),
      1);
}

class GrpcAccessLoggerCacheImplTest : public testing::Test {
public:
  GrpcAccessLoggerCacheImplTest()
      : async_client_(new Grpc::MockAsyncClient), factory_(new Grpc::MockAsyncClientFactory),
        logger_cache_(async_client_manager_, scope_, tls_, local_info_),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_, true) {
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
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Stats::Scope& scope_{*stats_store_.rootScope()};
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
  grpc_access_logger_impl_test_helper_.expectSentMessage(R"EOF(
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

  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
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
  grpc_access_logger_impl_test_helper_.expectSentMessage(R"EOF(
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
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
}

class GrpcAccessLoggerDisableBuiltinImplTest : public testing::Test {
public:
  GrpcAccessLoggerDisableBuiltinImplTest()
      : async_client_(new Grpc::MockAsyncClient), factory_(new Grpc::MockAsyncClientFactory),
        logger_cache_(async_client_manager_, scope_, tls_, local_info_),
        grpc_access_logger_impl_test_helper_(local_info_, async_client_, false) {
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
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Stats::Scope& scope_{*stats_store_.rootScope()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  GrpcAccessLoggerCacheImpl logger_cache_;
  GrpcAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
};

TEST_F(GrpcAccessLoggerDisableBuiltinImplTest, WithoutResourceAttributes) {
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.mutable_common_config()->set_log_name("test_log");
  config.mutable_common_config()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  // Force a flush for every log entry.
  config.mutable_common_config()->mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);
  config.set_disable_builtin_labels(true);

  GrpcAccessLoggerSharedPtr logger =
      logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP);
  grpc_access_logger_impl_test_helper_.expectSentMessage(R"EOF(
  resource_logs:
    resource:
      attributes:
    scope_logs:
      - log_records:
          - severity_text: "test-severity-text"
  )EOF");
  opentelemetry::proto::logs::v1::LogRecord entry;
  entry.set_severity_text("test-severity-text");
  logger->log(opentelemetry::proto::logs::v1::LogRecord(entry));
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
}

TEST_F(GrpcAccessLoggerDisableBuiltinImplTest, WithResourceAttributes) {
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.mutable_common_config()->set_log_name("test_log");
  config.mutable_common_config()->set_transport_api_version(
      envoy::config::core::v3::ApiVersion::V3);
  // Force a flush for every log entry.
  config.mutable_common_config()->mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);
  config.set_disable_builtin_labels(true);

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
  grpc_access_logger_impl_test_helper_.expectSentMessage(R"EOF(
  resource_logs:
    resource:
      attributes:
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
  EXPECT_EQ(stats_store_.findCounterByString("access_logs.open_telemetry_access_log.logs_written")
                .value()
                .get()
                .value(),
            1);
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
