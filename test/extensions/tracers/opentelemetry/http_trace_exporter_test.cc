#include <sys/types.h>

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
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
  OpenTelemetryHttpTraceExporterTest() {}

  void setup(envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config) {
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "fake_collector";
    cluster_manager_.initializeThreadLocalClusters({"fake_collector"});
    ON_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));

    cluster_manager_.initializeClusters({"fake_collector"}, {});

    trace_exporter_ =
        std::make_unique<OpenTelemetryHttpTraceExporter>(cluster_manager_, http_config);
  }

protected:
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<OpenTelemetryHttpTraceExporter> trace_exporter_;
};

TEST_F(OpenTelemetryHttpTraceExporterTest, CreateExporterAndExportSpan) {
  std::string yaml_string = fmt::format(R"EOF(
  http_uri:
    uri: "https://www.example.com/v1/traces"
    cluster: fake_collector
    timeout: 0.250s
  http_format: BINARY_PROTOBUF
  collector_path: "/v1/traces"
  collector_hostname: "example.com"
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

            EXPECT_EQ("/v1/traces", message->headers().getPathValue());
            EXPECT_EQ("example.com", message->headers().getHostValue());
            EXPECT_EQ(Http::Headers::get().ContentTypeValues.Protobuf,
                      message->headers().getContentTypeValue());

            return &request;
          }));

  std::string request_yaml = R"EOF(
    resource_spans:
      scope_spans:
        - spans:
          - name: "test"
  )EOF";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest
      export_trace_service_request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *export_trace_service_request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(trace_exporter_->log(export_trace_service_request));
}

TEST_F(OpenTelemetryHttpTraceExporterTest, UnsuccessfulLogWithoutThreadLocalCluster) {
  std::string yaml_string = fmt::format(R"EOF(
  http_uri:
    uri: "https://www.example.com/v1/traces"
    cluster: fake_collector
    timeout: 0.250s
  http_format: BINARY_PROTOBUF
  collector_path: "/v1/traces"
  collector_hostname: "example.com"
  )EOF");

  envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config;
  TestUtility::loadFromYaml(yaml_string, http_config);
  setup(http_config);

  Http::MockAsyncClientRequest request(&cluster_manager_.thread_local_cluster_.async_client_);

  ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("fake_collector")))
      .WillByDefault(Return(nullptr));

  std::string request_yaml = R"EOF(
    resource_spans:
      scope_spans:
        - spans:
          - name: "test"
  )EOF";
  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest
      export_trace_service_request;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *export_trace_service_request.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_FALSE(trace_exporter_->log(export_trace_service_request));
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
