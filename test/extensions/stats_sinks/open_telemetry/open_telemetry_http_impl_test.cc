#include "source/common/tracing/null_span_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_http_impl.h"

#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

class OpenTelemetryHttpMetricsExporterTest : public testing::Test {
public:
  void setup(envoy::config::core::v3::HttpService http_service) {
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "my_o11y_backend";
    cluster_manager_.initializeThreadLocalClusters({"my_o11y_backend"});
    ON_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
    cluster_manager_.initializeClusters({"my_o11y_backend"}, {});

    http_metrics_exporter_ =
        std::make_unique<OpenTelemetryHttpMetricsExporter>(cluster_manager_, http_service);
  }

  MetricsExportRequestPtr createTestMetricsRequest() {
    auto request = std::make_unique<MetricsExportRequest>();
    auto* resource_metrics = request->add_resource_metrics();
    auto* scope_metrics = resource_metrics->add_scope_metrics();
    auto* metric = scope_metrics->add_metrics();
    metric->set_name("test_metric");
    auto* gauge = metric->mutable_gauge();
    auto* data_point = gauge->add_data_points();
    data_point->set_as_int(42);
    return request;
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<OpenTelemetryHttpMetricsExporter> http_metrics_exporter_;
};

// Verifies OTLP HTTP export with custom headers, proper method, content-type, and user-agent.
TEST_F(OpenTelemetryHttpMetricsExporterTest, ExportMetricsWithCustomHeaders) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/v1/metrics"
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

            EXPECT_EQ("/v1/metrics", message->headers().getPathValue());
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

  http_metrics_exporter_->send(createTestMetricsRequest());

  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));

  // onBeforeFinalizeUpstreamSpan is a no-op, included for coverage.
  Tracing::NullSpan null_span;
  callback->onBeforeFinalizeUpstreamSpan(null_span, nullptr);

  callback->onSuccess(request, std::move(msg));
}

// Verifies that export is aborted gracefully when the cluster is not found.
TEST_F(OpenTelemetryHttpMetricsExporterTest, UnsuccessfulExportWithoutThreadLocalCluster) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/v1/metrics"
    cluster: "my_o11y_backend"
    timeout: 10s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("my_o11y_backend")))
      .WillByDefault(Return(nullptr));

  // The export should be dropped since cluster is not available.
  http_metrics_exporter_->send(createTestMetricsRequest());
}

// Verifies that non-success HTTP status codes (e.g., 503) are handled gracefully.
TEST_F(OpenTelemetryHttpMetricsExporterTest, ExportMetricsNonSuccessStatusCode) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/v1/metrics"
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

  http_metrics_exporter_->send(createTestMetricsRequest());

  // Simulate a 503 response.
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "503"}}}));
  callback->onSuccess(request, std::move(msg));
}

// Verifies that HTTP request failures (e.g., connection reset) are handled gracefully.
TEST_F(OpenTelemetryHttpMetricsExporterTest, ExportMetricsHttpFailure) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/v1/metrics"
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

  http_metrics_exporter_->send(createTestMetricsRequest());

  callback->onFailure(request, Http::AsyncClient::FailureReason::Reset);
}

// Verifies that when send_ returns nullptr, we don't track the request.
TEST_F(OpenTelemetryHttpMetricsExporterTest, SendReturnsNullptr) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/v1/metrics"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  // send_ returns nullptr (simulating immediate failure).
  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Return(nullptr));

  // Should handle nullptr return gracefully.
  http_metrics_exporter_->send(createTestMetricsRequest());
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
