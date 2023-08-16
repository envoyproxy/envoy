#include "http_trace_exporter.h"

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
    envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config,
    OpenTelemetryTracerStats& tracing_stats)
    : cluster_manager_(cluster_manager), http_config_(http_config), tracing_stats_(tracing_stats) {}

bool OpenTelemetryHttpTraceExporter::log(const ExportTraceServiceRequest& request) {

  std::string request_body;

  const auto ok = request.SerializeToString(&request_body);
  if (!ok) {
    ENVOY_LOG(warn, "Error while serializing the binary proto ExportTraceServiceRequest.");
    return false;
  }

  Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Protobuf);

  // If traces_path is omitted, send to /v1/traces by default
  if (http_config_.traces_path().empty()) {
    message->headers().setPath(TRACES_PATH);
  } else {
    message->headers().setPath(http_config_.traces_path());
  }

  // TODO: Can we get the hostname that is configured in the cluster "socker_address" field?
  message->headers().setHost(http_config_.hostname());

  // add all custom headers to the request
  for (const envoy::config::core::v3::HeaderValue& header : http_config_.headers()) {
    message->headers().setCopy(Http::LowerCaseString(header.key()), header.value());
  }

  message->body().add(request_body);

  const auto thread_local_cluster =
      cluster_manager_.getThreadLocalCluster(http_config_.cluster_name());
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(warn, "Thread local cluster not found for collector.");
    return false;
  }

  std::chrono::milliseconds timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::nanoseconds(http_config_.timeout().nanos()));
  Http::AsyncClient::Request* http_request = thread_local_cluster->httpAsyncClient().send(
      std::move(message), *this, Http::AsyncClient::RequestOptions().setTimeout(timeout));
  tracing_stats_.http_reports_sent_.inc();

  return http_request;
}

void OpenTelemetryHttpTraceExporter::onSuccess(const Http::AsyncClient::Request&,
                                               Http::ResponseMessagePtr&& message) {
  tracing_stats_.http_reports_success_.inc();
  const auto response_code = message->headers().Status()->value().getStringView();
  if (response_code != "200") {
    ENVOY_LOG(warn, "response code: {}", response_code);
  }
}

void OpenTelemetryHttpTraceExporter::onFailure(const Http::AsyncClient::Request&,
                                               Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "Request failed.");
  tracing_stats_.http_reports_failed_.inc();
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
