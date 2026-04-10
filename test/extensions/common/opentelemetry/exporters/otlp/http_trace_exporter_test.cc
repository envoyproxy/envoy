#include "source/common/version/version.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/http_trace_exporter.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

class OtlpHttpTraceExporterTest : public testing::Test {
public:
  OtlpHttpTraceExporterTest() {
    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");

    ON_CALL(context_.server_factory_context_, mainThreadDispatcher())
        .WillByDefault(ReturnRef(*dispatcher_));
    ON_CALL(context_.server_factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  void setup(envoy::config::core::v3::HttpService http_service) {
    cluster_manager_.thread_local_cluster_.cluster_.info_->name_ = "my_o11y_backend";
    cluster_manager_.initializeThreadLocalClusters({"my_o11y_backend"});
    ON_CALL(cluster_manager_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(cluster_manager_.thread_local_cluster_.async_client_));
    cluster_manager_.initializeClusters({"my_o11y_backend"}, {});

    auto headers_applicator = Http::HttpServiceHeadersApplicator::createOrThrow(
        http_service, context_.server_factory_context_);
    trace_exporter_ = std::make_unique<OtlpHttpTraceExporter>(cluster_manager_, http_service,
                                                              std::move(headers_applicator));
  }

protected:
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<OtlpHttpTraceExporter> trace_exporter_;
  NiceMock<Envoy::Server::Configuration::MockTracerFactoryContext> context_;
};

// Happy path: log() serializes and sends the request, verifying headers and body.
// onSuccess with 200 OK must not log an error.
// onFailure must log at debug level.
TEST_F(OtlpHttpTraceExporterTest, CreateExporterAndExportSpan) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/traces"
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

            EXPECT_EQ(Http::Headers::get().MethodValues.Post, message->headers().getMethodValue());
            EXPECT_EQ(Http::Headers::get().ContentTypeValues.Protobuf,
                      message->headers().getContentTypeValue());
            EXPECT_EQ(message->headers().getUserAgentValue(),
                      "OTel-OTLP-Exporter-Envoy/" + Envoy::VersionInfo::version());
            EXPECT_EQ("/otlp/v1/traces", message->headers().getPathValue());
            EXPECT_EQ("some-o11y.com", message->headers().getHostValue());
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

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest req;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *req.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(trace_exporter_->log(req));

  // onSuccess with 200 OK — no error log.
  Http::ResponseMessagePtr ok_msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
  EXPECT_LOG_NOT_CONTAINS("error", "OTLP HTTP exporter received",
                          callback->onSuccess(request, std::move(ok_msg)));

  // onFailure — debug log.
  EXPECT_LOG_CONTAINS("debug", "The OTLP export request failed",
                      callback->onFailure(request, Http::AsyncClient::FailureReason::Reset));
}

// onSuccess with a non-OK status code must emit an error log.
TEST_F(OtlpHttpTraceExporterTest, OnSuccessNonOkStatusLogsError) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/traces"
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

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest req;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *req.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_TRUE(trace_exporter_->log(req));

  Http::ResponseMessagePtr err_msg(new Http::ResponseMessageImpl(
      Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", "502"}}}));
  EXPECT_LOG_CONTAINS("error", "OTLP HTTP exporter received a non-success status code",
                      callback->onSuccess(request, std::move(err_msg)));
}

// log() must return false when the thread-local cluster is not found.
TEST_F(OtlpHttpTraceExporterTest, LogReturnsFalseWhenClusterMissing) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/traces"
    cluster: "my_o11y_backend"
    timeout: 10s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  ON_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view("my_o11y_backend")))
      .WillByDefault(Return(nullptr));

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest req;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  *req.add_resource_spans()->add_scope_spans()->add_spans() = span;
  EXPECT_LOG_CONTAINS("error", "OTLP HTTP exporter failed",
                      EXPECT_FALSE(trace_exporter_->log(req)));
}

// log() must return false when the async client returns a null request.
// Also verifies that the debug span-count log is still emitted before returning.
TEST_F(OtlpHttpTraceExporterTest, LogReturnsFalseWhenAsyncClientReturnsNull) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/traces"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  EXPECT_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Return(nullptr));

  opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest req;
  opentelemetry::proto::trace::v1::Span span;
  span.set_name("test");
  auto* resource_spans = req.add_resource_spans();
  resource_spans->mutable_resource();
  auto* scope_spans = resource_spans->add_scope_spans();
  scope_spans->mutable_scope();
  *scope_spans->add_spans() = span;

  EXPECT_LOG_CONTAINS("debug", "Number of exported spans: 1",
                      EXPECT_FALSE(trace_exporter_->log(req)));
}

// onBeforeFinalizeUpstreamSpan is a no-op override required by the interface; verify it can be
// called without crashing.
TEST_F(OtlpHttpTraceExporterTest, OnBeforeFinalizeUpstreamSpanIsNoOp) {
  std::string yaml_string = R"EOF(
  http_uri:
    uri: "https://some-o11y.com/otlp/v1/traces"
    cluster: "my_o11y_backend"
    timeout: 0.250s
  )EOF";

  envoy::config::core::v3::HttpService http_service;
  TestUtility::loadFromYaml(yaml_string, http_service);
  setup(http_service);

  Tracing::NullSpan null_span;
  trace_exporter_->onBeforeFinalizeUpstreamSpan(null_span, nullptr);
}

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
