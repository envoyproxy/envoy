#include <chrono>
#include <memory>

#include "envoy/common/time.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/access_loggers/open_telemetry/access_log_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

using namespace std::chrono_literals;
using ::Envoy::AccessLog::FilterPtr;
using ::Envoy::AccessLog::MockFilter;
using opentelemetry::proto::logs::v1::LogRecord;
using testing::_;
using testing::An;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {
namespace {

class MockGrpcAccessLogger : public GrpcAccessLogger {
public:
  // GrpcAccessLogger
  MOCK_METHOD(void, log, (LogRecord && entry));
  MOCK_METHOD(void, log, (ProtobufWkt::Empty && entry));
};

class MockGrpcAccessLoggerCache : public GrpcAccessLoggerCache {
public:
  // GrpcAccessLoggerCache
  MOCK_METHOD(GrpcAccessLoggerSharedPtr, getOrCreateLogger,
              (const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig& config,
               envoy::config::core::v3::ApiVersion, Common::GrpcAccessLoggerType logger_type,
               Stats::Scope& scope));
};

class AccessLogTest : public testing::Test {
public:
  void initAdminAccessLog() {
    ON_CALL(*filter_, evaluate(_, _, _, _)).WillByDefault(Return(true));

    TestUtility::loadFromYaml(R"EOF(
string_value: "x-request-header: %REQ(x-request-header)%, protocol: %PROTOCOL%"
)EOF",
                              *config_.mutable_body());

    TestUtility::loadFromYaml(R"EOF(
values:
  - key: "status_code"
    value:
      string_value: "%RESPONSE_CODE%"
  - key: "duration_ms"
    value:
      string_value: "%REQUEST_DURATION%"
  - key: "request_bytes"
    value:
      string_value: "%BYTES_RECEIVED%"
)EOF",
                              *config_.mutable_attributes());

    config_.mutable_common_config()->set_log_name("test_log");
    config_.mutable_common_config()->set_transport_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    EXPECT_CALL(*logger_cache_, getOrCreateLogger(_, _, _, _))
        .WillOnce(
            [this](const envoy::extensions::access_loggers::grpc::v3::CommonGrpcAccessLogConfig&
                       config,
                   envoy::config::core::v3::ApiVersion, Common::GrpcAccessLoggerType logger_type,
                   Stats::Scope&) {
              EXPECT_EQ(config.DebugString(), config_.common_config().DebugString());
              EXPECT_EQ(Common::GrpcAccessLoggerType::HTTP, logger_type);
              return logger_;
            });
    access_log_ =
        std::make_unique<AccessLog>(FilterPtr{filter_}, config_, tls_, logger_cache_, scope_);
  }

  void expectLog(const std::string& expected_log_entry_yaml) {
    if (access_log_ == nullptr) {
      initAdminAccessLog();
    }

    LogRecord expected_log_entry;
    TestUtility::loadFromYaml(expected_log_entry_yaml, expected_log_entry);
    EXPECT_CALL(*logger_, log(An<LogRecord&&>()))
        .WillOnce(Invoke([expected_log_entry](LogRecord&& entry) {
          EXPECT_EQ(entry.DebugString(), expected_log_entry.DebugString());
        }));
  }

  Stats::IsolatedStoreImpl scope_;
  MockFilter* filter_{new NiceMock<MockFilter>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  envoy::extensions::access_loggers::open_telemetry::v3alpha::OpenTelemetryAccessLogConfig config_;
  std::shared_ptr<MockGrpcAccessLogger> logger_{new MockGrpcAccessLogger()};
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache_{new MockGrpcAccessLoggerCache()};
  AccessLogPtr access_log_;
};

// Test log marshaling.
TEST_F(AccessLogTest, Marshalling) {
  InSequence s;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.start_time_ = SystemTime(1h);
  stream_info.last_downstream_rx_byte_received_ = 2ms;
  stream_info.protocol_ = Http::Protocol::Http10;
  stream_info.addBytesReceived(10);
  stream_info.response_code_ = 200;

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-header", "test-request-header"},
  };
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  expectLog(R"EOF(
      time_unix_nano: 3600000000000
      body:
        string_value: "x-request-header: test-request-header, protocol: HTTP/1.0"
      attributes:
        - key: "status_code"
          value:
            string_value: "200"
        - key: "duration_ms"
          value:
            string_value: "2"
        - key: "request_bytes"
          value:
            string_value: "10"
    )EOF");
  access_log_->log(&request_headers, &response_headers, nullptr, stream_info);
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
