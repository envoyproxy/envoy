#include "source/common/http/http_service_headers.h"
#include "source/extensions/access_loggers/open_telemetry/http_access_log_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

const std::string ZONE_NAME = "test_zone";
const std::string CLUSTER_NAME = "test_cluster";
const std::string NODE_NAME = "test_node";

class HttpAccessLoggerImplTest : public testing::Test {
public:
  HttpAccessLoggerImplTest() : timer_(new Event::MockTimer(&dispatcher_)) {
    EXPECT_CALL(*timer_, enableTimer(_, _)).Times(testing::AnyNumber());
  }

  void setup(envoy::config::core::v3::HttpService http_service) {
    envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
    setupWithConfig(http_service, config);
  }

  void setupWithConfig(
      envoy::config::core::v3::HttpService http_service,
      envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config) {
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "my_o11y_backend";
    cluster_manager_.initializeThreadLocalClusters({"my_o11y_backend"});
    ON_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

    cluster_manager_.initializeClusters({"my_o11y_backend"}, {});

    ON_CALL(factory_context_.server_factory_context_.local_info_, zoneName())
        .WillByDefault(ReturnRef(ZONE_NAME));
    ON_CALL(factory_context_.server_factory_context_.local_info_, clusterName())
        .WillByDefault(ReturnRef(CLUSTER_NAME));
    ON_CALL(factory_context_.server_factory_context_.local_info_, nodeName())
        .WillByDefault(ReturnRef(NODE_NAME));

    auto headers_applicator = Http::HttpServiceHeadersApplicator::createOrThrow(
        http_service, factory_context_.server_factory_context_);
    http_access_logger_ = std::make_unique<HttpAccessLoggerImpl>(
        cluster_manager_, http_service, std::move(headers_applicator), config, dispatcher_,
        factory_context_.server_factory_context_);
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* timer_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  std::unique_ptr<HttpAccessLoggerImpl> http_access_logger_;
};

// Verifies OTLP HTTP export with custom headers, proper method, content-type, and user-agent.
TEST_F(HttpAccessLoggerImplTest, CreateExporterAndExportLog) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  request_headers_to_add:
  - header:
      key: "Authorization"
      value: "auth-token"
  - header:
      key: "x-custom-header"
      value: "custom-value"
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;

  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_,
              send_(_, _,
                    Http::AsyncClient::RequestOptions()
                        .setTimeout(std::chrono::milliseconds(250))
                        .setDiscardResponseBody(true)))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            // Verify OTLP HTTP spec compliance: POST method and protobuf content-type.
            EXPECT_EQ(Http::Headers::get().MethodValues.Post, message->headers().getMethodValue());
            EXPECT_EQ(Http::Headers::get().ContentTypeValues.Protobuf,
                      message->headers().getContentTypeValue());

            EXPECT_EQ("/otlp/v1/logs", message->headers().getPathValue());
            EXPECT_EQ("some-o11y.com", message->headers().getHostValue());

            // Verify User-Agent follows OTLP spec.
            EXPECT_TRUE(absl::StartsWith(message->headers().getUserAgentValue(),
                                         "OTel-OTLP-Exporter-Envoy/"));

            // Custom headers provided in the configuration.
            EXPECT_EQ("auth-token", message->headers()
                                        .get(Http::LowerCaseString("authorization"))[0]
                                        ->value()
                                        .getStringView());
            EXPECT_EQ("custom-value", message->headers()
                                          .get(Http::LowerCaseString("x-custom-header"))[0]
                                          ->value()
                                          .getStringView());

            return &request;
          }));

  opentelemetry::proto::logs::v1::LogRecord log_record;
  log_record.set_severity_number(opentelemetry::proto::logs::v1::SEVERITY_NUMBER_INFO);
  log_record.mutable_body()->set_string_value("test log message");
  http_access_logger_->log(std::move(log_record));

  // Trigger flush via timer callback.
  timer_->invokeCallback();

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));

  // onBeforeFinalizeUpstreamSpan is a no-op, included for coverage.
  Tracing::NullSpan null_span;
  callback->onBeforeFinalizeUpstreamSpan(null_span, nullptr);

  callback->onSuccess(request, std::move(msg));
}

// Verifies that export is aborted gracefully when the cluster is not found.
TEST_F(HttpAccessLoggerImplTest, UnsuccessfulLogWithoutThreadLocalCluster) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 10s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("my_o11y_backend")))
      .WillByDefault(Return(nullptr));

  opentelemetry::proto::logs::v1::LogRecord log_record;
  log_record.set_severity_number(opentelemetry::proto::logs::v1::SEVERITY_NUMBER_INFO);
  log_record.mutable_body()->set_string_value("test log message");
  http_access_logger_->log(std::move(log_record));

  // Trigger flush via timer callback - the log should be dropped since cluster is not available.
  timer_->invokeCallback();
}

// Verifies that non-success HTTP status codes (e.g., 503) are handled gracefully.
TEST_F(HttpAccessLoggerImplTest, ExportLogsNonSuccessStatusCode) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;

  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;
            return &request;
          }));

  opentelemetry::proto::logs::v1::LogRecord log_record;
  log_record.set_severity_number(opentelemetry::proto::logs::v1::SEVERITY_NUMBER_ERROR);
  log_record.mutable_body()->set_string_value("error log message");
  http_access_logger_->log(std::move(log_record));

  // Trigger flush via timer callback.
  timer_->invokeCallback();

  // Simulate a 503 response.
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
  callback->onSuccess(request, std::move(msg));
}

// Verifies that HTTP request failures (e.g., connection reset) are handled gracefully.
TEST_F(HttpAccessLoggerImplTest, ExportLogsHttpFailure) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;

  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;
            return &request;
          }));

  opentelemetry::proto::logs::v1::LogRecord log_record;
  log_record.set_severity_number(opentelemetry::proto::logs::v1::SEVERITY_NUMBER_INFO);
  log_record.mutable_body()->set_string_value("test log message");
  http_access_logger_->log(std::move(log_record));

  // Trigger flush via timer callback.
  timer_->invokeCallback();

  callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

// Verifies that flush with no log records is a no-op (doesn't send a request).
TEST_F(HttpAccessLoggerImplTest, FlushWithNoLogRecordsIsNoOp) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  // No send call should be made since there are no logs.
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _)).Times(0);

  // Trigger flush via timer callback with no logs buffered.
  timer_->invokeCallback();
}

// Verifies that when send_ returns nullptr, we don't track the request.
TEST_F(HttpAccessLoggerImplTest, SendReturnsNullptr) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  // send_ returns nullptr (simulating immediate failure).
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Return(nullptr));

  opentelemetry::proto::logs::v1::LogRecord log_record;
  log_record.set_severity_number(opentelemetry::proto::logs::v1::SEVERITY_NUMBER_INFO);
  log_record.mutable_body()->set_string_value("test log message");
  http_access_logger_->log(std::move(log_record));

  // Trigger flush via timer callback - should handle nullptr return gracefully.
  timer_->invokeCallback();
}

// Verifies that buffer overflow triggers immediate flush.
TEST_F(HttpAccessLoggerImplTest, BufferOverflowTriggersFlush) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);

  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  // Set a very small buffer size to trigger overflow.
  config.mutable_buffer_size_bytes()->set_value(1);
  setupWithConfig(http_service, config);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;

  // Expect a flush triggered by buffer overflow (not timer).
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;
            return &request;
          }));

  opentelemetry::proto::logs::v1::LogRecord log_record;
  log_record.set_severity_number(opentelemetry::proto::logs::v1::SEVERITY_NUMBER_INFO);
  log_record.mutable_body()->set_string_value("test log message that exceeds buffer");
  // This should trigger immediate flush due to buffer overflow.
  http_access_logger_->log(std::move(log_record));

  // Complete the request.
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  callback->onSuccess(request, std::move(msg));
}

// Verifies that getOrCreateLogger returns the same logger instance for identical config.
TEST(HttpAccessLoggerCacheTest, CacheHitReturnsSameLogger) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);

  envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig config;
  config.set_log_name("test_log");

  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  factory_context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
      ->name_ = "my_o11y_backend";
  factory_context.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {"my_o11y_backend"});
  factory_context.server_factory_context_.cluster_manager_.initializeClusters({"my_o11y_backend"},
                                                                              {});

  ON_CALL(factory_context.server_factory_context_.local_info_, zoneName())
      .WillByDefault(ReturnRef(ZONE_NAME));
  ON_CALL(factory_context.server_factory_context_.local_info_, clusterName())
      .WillByDefault(ReturnRef(CLUSTER_NAME));
  ON_CALL(factory_context.server_factory_context_.local_info_, nodeName())
      .WillByDefault(ReturnRef(NODE_NAME));

  auto cache = std::make_shared<HttpAccessLoggerCacheImpl>(factory_context.server_factory_context_);

  std::shared_ptr<const Http::HttpServiceHeadersApplicator> headers_applicator =
      Http::HttpServiceHeadersApplicator::createOrThrow(http_service,
                                                        factory_context.server_factory_context_);

  auto logger1 = cache->getOrCreateLogger(config, http_service, headers_applicator);
  ASSERT_NE(nullptr, logger1);

  auto logger2 = cache->getOrCreateLogger(config, http_service, headers_applicator);
  EXPECT_EQ(logger1.get(), logger2.get());
}

// Verifies that failure in creation of the applicator is handled correctly through
// the cache.
TEST(HttpAccessLoggerCacheTest, CreateApplicatorFailure) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/logs"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  request_headers_to_add:
    - header:
        key: "x-bad-formatter"
        value: "%UNCLOSED_FORMATTER"
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context;

  auto cache = std::make_shared<HttpAccessLoggerCacheImpl>(server_context);

  EXPECT_THROW(cache->getOrCreateApplicator(http_service, server_context), EnvoyException);
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
