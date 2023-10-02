#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"

#include <chrono>
#include <memory>
#include <string>

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

OpenTelemetryHttpTraceExporter::OpenTelemetryHttpTraceExporter(
    Upstream::ClusterManager& cluster_manager,
    const envoy::config::core::v3::HttpService& http_service,
    OpenTelemetryTracerStats& tracing_stats)
    : cluster_manager_(cluster_manager), http_service_(http_service),
      tracing_stats_(tracing_stats) {}

bool OpenTelemetryHttpTraceExporter::log(const ExportTraceServiceRequest& request) {

  std::string request_body;

  const auto ok = request.SerializeToString(&request_body);
  if (!ok) {
    ENVOY_LOG(warn, "Error while serializing the binary proto ExportTraceServiceRequest.");
    return false;
  }

  const auto thread_local_cluster =
      cluster_manager_.getThreadLocalCluster(http_service_.http_uri().cluster());
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "OTLP HTTP exporter failed: [cluster = {}] is not configured",
              http_service_.http_uri().cluster());
    return false;
  }

  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(http_service_.http_uri());

  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Protobuf);

  // add all custom headers to the request
  for (const auto& header_value_option : http_service_.request_headers_to_add()) {
    message->headers().setCopy(Http::LowerCaseString(header_value_option.header().key()),
                               header_value_option.header().value());
  }
  message->body().add(request_body);

  auto options = Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(http_service_.http_uri().timeout())));

  Http::AsyncClient::Request* http_request =
      thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);
  tracing_stats_.http_reports_sent_.inc();

  return http_request;
}

void OpenTelemetryHttpTraceExporter::onSuccess(const Http::AsyncClient::Request&,
                                               Http::ResponseMessagePtr&& message) {
  tracing_stats_.http_reports_success_.inc();
  const auto response_code = message->headers().Status()->value().getStringView();
  if (response_code != "200") {
    ENVOY_LOG(error, "OTLP HTTP exporter received a non-success status code: {} while exporting the OTLP message", response_code);
  }
}

void OpenTelemetryHttpTraceExporter::onFailure(const Http::AsyncClient::Request&,
                                               Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "The OTLP export request failed.");
  tracing_stats_.http_reports_failed_.inc();
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
