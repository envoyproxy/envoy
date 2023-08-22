#include <sys/types.h>

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

class OpenTelemetryHttpTraceExporterTest : public testing::Test {
public:
  OpenTelemetryHttpTraceExporterTest() = default;

  void setup(envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config) {
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "my_o11y_backend";
    cluster_manager_.initializeThreadLocalClusters({"my_o11y_backend"});
    ON_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

    cluster_manager_.initializeClusters({"my_o11y_backend"}, {});

    trace_exporter_ =
        std::make_unique<OpenTelemetryHttpTraceExporter>(cluster_manager_, http_config, stats_);
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<OpenTelemetryHttpTraceExporter> trace_exporter_;
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
  NiceMock<Stats::MockIsolatedStatsStore>& mock_scope_ = context_.server_factory_context_.store_;
  OpenTelemetryTracerStats stats_{OpenTelemetryTracerStats{
      OPENTELEMETRY_TRACER_STATS(POOL_COUNTER_PREFIX(mock_scope_, "tracing.opentelemetry."))}};
};

TEST_F(OpenTelemetryHttpTraceExporterTest, CreateExporterAndExportSpan) {
  std::string yaml_string = fmt::format(R"EOF(
  cluster_name: "my_o11y_backend"
  traces_path: "/otlp/v1/traces"
  hostname: "some-o11y.com"
  headers:
    - key: "Authorization"
      value: "auth-token"
    - key: "x-custom-header"
      value: "custom-value"
  timeout: 0.250s
  )EOF");

  envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config;
  TestUtility::loadFromYaml(yaml_string, http_config);
  setup(http_config);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;

  EXPECT_CALL(
      cluster_manager_.thread_local_cluster_.async_client_,
      send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(250))))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            EXPECT_EQ(Http::Headers::get().MethodValues.Post, message->headers().getMethodValue());
            EXPECT_EQ(Http::Headers::get().ContentTypeValues.Protobuf,
                      message->headers().getContentTypeValue());

            EXPECT_EQ("/otlp/v1/traces", message->headers().getPathValue());
            EXPECT_EQ("some-o11y.com", message->headers().getHostValue());

            // Custom headers provided in the configuration
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

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest
      export_trace_service_request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *export_trace_service_request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(trace_exporter_->log(export_trace_service_request));

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));
  // onBeforeFinalizeUpstreamSpan is a noop — included for coverage
  Tracing::NullSpan null_span;
  callback->onBeforeFinalizeUpstreamSpan(null_span, nullptr);

  callback->onSuccess(request, std::move(msg));
  EXPECT_EQ(1U, mock_scope_.counter("tracing.opentelemetry.http_reports_sent").value());
  EXPECT_EQ(1U, mock_scope_.counter("tracing.opentelemetry.http_reports_success").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.opentelemetry.http_reports_failed").value());

  callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, mock_scope_.counter("tracing.opentelemetry.http_reports_failed").value());
}

TEST_F(OpenTelemetryHttpTraceExporterTest, UnsuccessfulLogWithoutThreadLocalCluster) {
  std::string yaml_string = fmt::format(R"EOF(
  cluster_name: "my_o11y_backend"
  traces_path: "/otlp/v1/traces"
  hostname: "some-o11y.com"
  timeout: 0.250s
  )EOF");

  envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config;
  TestUtility::loadFromYaml(yaml_string, http_config);
  setup(http_config);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);

  ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("my_o11y_backend")))
      .WillByDefault(Return(nullptr));

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest
      export_trace_service_request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *export_trace_service_request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_FALSE(trace_exporter_->log(export_trace_service_request));
}

TEST_F(OpenTelemetryHttpTraceExporterTest, CreateExporterAndExportSpanWithDefaultPath) {
  std::string yaml_string = fmt::format(R"EOF(
  cluster_name: "my_o11y_backend"
  hostname: "some-o11y.com"
  timeout: 0.250s
  )EOF");

  envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config;
  TestUtility::loadFromYaml(yaml_string, http_config);
  setup(http_config);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);
  Http::AsyncClient::Callbacks* callback;

  EXPECT_CALL(
      cluster_manager_.thread_local_cluster_.async_client_,
      send_(_, _, Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(250))))
      .WillOnce(
          Invoke([&](Http::RequestMessagePtr& message, Http::AsyncClient::Callbacks& callbacks,
                     const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
            callback = &callbacks;

            // Default path is used when omitted in the config
            EXPECT_EQ("/v1/traces", message->headers().getPathValue());

            return &request;
          }));

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest
      export_trace_service_request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *export_trace_service_request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(trace_exporter_->log(export_trace_service_request));

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "202"}}}));
  // onBeforeFinalizeUpstreamSpan is a noop — included for coverage
  Tracing::NullSpan null_span;
  callback->onBeforeFinalizeUpstreamSpan(null_span, nullptr);

  callback->onSuccess(request, std::move(msg));
  EXPECT_EQ(1U, mock_scope_.counter("tracing.opentelemetry.http_reports_sent").value());
  EXPECT_EQ(1U, mock_scope_.counter("tracing.opentelemetry.http_reports_success").value());
  EXPECT_EQ(0U, mock_scope_.counter("tracing.opentelemetry.http_reports_failed").value());

  callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(1U, mock_scope_.counter("tracing.opentelemetry.http_reports_failed").value());
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
