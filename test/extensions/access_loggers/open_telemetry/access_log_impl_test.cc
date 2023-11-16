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
#include "test/mocks/common.h"
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
using opentelemetry::proto::common::v1::AnyValue;
using opentelemetry::proto::common::v1::KeyValueList;
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
  MOCK_METHOD(
      GrpcAccessLoggerSharedPtr, getOrCreateLogger,
      (const envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig&
           config,
       Common::GrpcAccessLoggerType logger_type));
};

class AccessLogTest : public testing::Test {
public:
  AccessLogPtr makeAccessLog(const AnyValue& body_config, const KeyValueList& attributes_config) {
    ON_CALL(*filter_, evaluate(_, _)).WillByDefault(Return(true));
    *config_.mutable_body() = body_config;
    *config_.mutable_attributes() = attributes_config;
    config_.mutable_common_config()->set_log_name("test_log");
    config_.mutable_common_config()->set_transport_api_version(
        envoy::config::core::v3::ApiVersion::V3);
    EXPECT_CALL(*logger_cache_, getOrCreateLogger(_, _))
        .WillOnce([this](const envoy::extensions::access_loggers::open_telemetry::v3::
                             OpenTelemetryAccessLogConfig& config,
                         Common::GrpcAccessLoggerType logger_type) {
          EXPECT_EQ(config.DebugString(), config_.DebugString());
          EXPECT_EQ(Common::GrpcAccessLoggerType::HTTP, logger_type);
          return logger_;
        });
    return std::make_unique<AccessLog>(FilterPtr{filter_}, config_, tls_, logger_cache_);
  }

  void expectLog(const std::string& expected_log_entry_yaml) {
    LogRecord expected_log_entry;
    TestUtility::loadFromYaml(expected_log_entry_yaml, expected_log_entry);
    EXPECT_CALL(*logger_, log(An<LogRecord&&>()))
        .WillOnce(Invoke([expected_log_entry](LogRecord&& entry) {
          EXPECT_EQ(entry.DebugString(), expected_log_entry.DebugString());
        }));
  }

  MockFilter* filter_{new NiceMock<MockFilter>()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config_;
  std::shared_ptr<MockGrpcAccessLogger> logger_{new MockGrpcAccessLogger()};
  std::shared_ptr<MockGrpcAccessLoggerCache> logger_cache_{new MockGrpcAccessLoggerCache()};
};

// Test log marshaling.
TEST_F(AccessLogTest, Marshalling) {
  InSequence s;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.start_time_ = SystemTime(1h);
  MockTimeSystem time_system;
  EXPECT_CALL(time_system, monotonicTime)
      .WillOnce(Return(MonotonicTime(std::chrono::milliseconds(2))));
  stream_info.downstream_timing_.onLastDownstreamRxByteReceived(time_system);
  stream_info.protocol_ = Http::Protocol::Http10;
  stream_info.addBytesReceived(10);
  stream_info.setResponseCode(200);

  Http::TestRequestHeaderMapImpl request_headers{
      {"x-request-header", "test-request-header"},
  };
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  const auto body_config = TestUtility::parseYaml<AnyValue>(R"EOF(
    string_value: "x-request-header: %REQ(x-request-header)%, protocol: %PROTOCOL%"
    )EOF");
  const auto attributes_config = TestUtility::parseYaml<KeyValueList>(R"EOF(
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
    )EOF");
  auto access_log = makeAccessLog(body_config, attributes_config);
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
  access_log->log({&request_headers, &response_headers}, stream_info);
}

// Test log with empty config.
TEST_F(AccessLogTest, EmptyConfig) {
  InSequence s;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.start_time_ = SystemTime(1h);
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  auto access_log = makeAccessLog({}, {});
  expectLog(R"EOF(
      time_unix_nano: 3600000000000
    )EOF");
  access_log->log({&request_headers, &response_headers}, stream_info);
}

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
