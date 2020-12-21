#include <memory>

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"

#include "extensions/access_loggers/grpc/grpc_ot_access_log_impl.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

// opentelemetry::proto::logs::v1::LogRecord,
// opentelemetry::proto::logs::v1::ResourceLogs /*TCP*/,
// opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest,
// opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse>

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace GrpcCommon {
namespace {

constexpr std::chrono::milliseconds FlushInterval(10);
constexpr int BUFFER_SIZE_BYTES = 0;

// A helper test class to mock and intercept GrpcOpenTelemetryAccessLoggerImpl streams.
class GrpcOpenTelemetryAccessLoggerImplTestHelper {
public:
  using MockAccessLogStream = Grpc::MockAsyncStream;
  using AccessLogCallbacks = Grpc::AsyncStreamCallbacks<
      opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse>;

  GrpcOpenTelemetryAccessLoggerImplTestHelper(Grpc::MockAsyncClient* async_client) {
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

class GrpcOpenTelemetryAccessLoggerImplTest : public testing::Test {
public:
  GrpcOpenTelemetryAccessLoggerImplTest()
      : async_client_(new Grpc::MockAsyncClient), timer_(new Event::MockTimer(&dispatcher_)),
        grpc_access_logger_impl_test_helper_(async_client_) {
    EXPECT_CALL(*timer_, enableTimer(_, _));
    fmt::print("before");
    logger_ = std::make_unique<GrpcOpenTelemetryAccessLoggerImpl>(
        Grpc::RawAsyncClientPtr{async_client_}, FlushInterval, BUFFER_SIZE_BYTES, dispatcher_,
        stats_store_, envoy::config::core::v3::ApiVersion::V3);
    fmt::print("after");
  }

  Grpc::MockAsyncClient* async_client_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* timer_;
  std::unique_ptr<GrpcOpenTelemetryAccessLoggerImpl> logger_;
  GrpcOpenTelemetryAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
};

TEST_F(GrpcOpenTelemetryAccessLoggerImplTest, LogHttp) {
  grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
  resource_logs:
    - instrumentation_library_logs:
      - instrumentation_library:
          name: "envoy"
          version: "v3"
        logs:
          - severity_text: "iitamark"
  )EOF");
  opentelemetry::proto::logs::v1::LogRecord entry;
  entry.set_severity_text("iitamark");
  logger_->log(opentelemetry::proto::logs::v1::LogRecord(entry));
}

// TEST_F(GrpcOpenTelemetryAccessLoggerImplTest, LogTcp) {
//   grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
// identifier:
//   node:
//     id: node_name
//     cluster: cluster_name
//     locality:
//       zone: zone_name
//   log_name: test_log_name
// tcp_logs:
//   log_entry:
//     common_properties:
//       sample_rate: 1.0
// )EOF");
//   envoy::data::accesslog::v3::TCPAccessLogEntry tcp_entry;
//   tcp_entry.mutable_common_properties()->set_sample_rate(1);
//   logger_->log(envoy::data::accesslog::v3::TCPAccessLogEntry(tcp_entry));
// }

// class GrpcOpenTelemetryAccessLoggerCacheImplTest : public testing::Test {
// public:
//   GrpcOpenTelemetryAccessLoggerCacheImplTest()
//       : async_client_(new Grpc::MockAsyncClient), factory_(new Grpc::MockAsyncClientFactory),
//         logger_cache_(async_client_manager_, scope_, tls_),
//         grpc_access_logger_impl_test_helper_(async_client_) {
//     EXPECT_CALL(async_client_manager_, factoryForGrpcService(_, _, false))
//         .WillOnce(Invoke([this](const envoy::config::core::v3::GrpcService&, Stats::Scope&, bool)
//         {
//           EXPECT_CALL(*factory_, create()).WillOnce(Invoke([this] {
//             return Grpc::RawAsyncClientPtr{async_client_};
//           }));
//           return Grpc::AsyncClientFactoryPtr{factory_};
//         }));
//   }

//   Grpc::MockAsyncClient* async_client_;
//   Grpc::MockAsyncClientFactory* factory_;
//   Grpc::MockAsyncClientManager async_client_manager_;
//   NiceMock<Stats::MockIsolatedStatsStore> scope_;
//   NiceMock<ThreadLocal::MockInstance> tls_;
//   GrpcOpenTelemetryAccessLoggerCacheImpl logger_cache_;
//   GrpcOpenTelemetryAccessLoggerImplTestHelper grpc_access_logger_impl_test_helper_;
// };

// // Test that the logger is created according to the config (by inspecting the generated log).
// TEST_F(GrpcOpenTelemetryAccessLoggerCacheImplTest, LoggerCreation) {
//   envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig config;
//   config.set_log_name("test-log");
//   config.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
//   // Force a flush for every log entry.
//   config.mutable_buffer_size_bytes()->set_value(BUFFER_SIZE_BYTES);

//   GrpcAccessLoggerSharedPtr logger =
//       logger_cache_.getOrCreateLogger(config, Common::GrpcAccessLoggerType::HTTP, scope_);
//   (void)logger;
//   // Note that the local info node() method is mocked, so the node is not really configurable.
//   // grpc_access_logger_impl_test_helper_.expectStreamMessage(R"EOF(
//   // identifier:
//   //   node:
//   //     id: node_name
//   //     cluster: cluster_name
//   //     locality:
//   //       zone: zone_name
//   //   log_name: test-log
//   // http_logs:
//   //   log_entry:
//   //     request:
//   //       path: /test/path1
//   // )EOF");
//   // envoy::data::accesslog::v3::HTTPAccessLogEntry entry;
//   // entry.mutable_request()->set_path("/test/path1");
//   // logger->log(envoy::data::accesslog::v3::HTTPAccessLogEntry(entry));
// }

} // namespace
} // namespace GrpcCommon
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
