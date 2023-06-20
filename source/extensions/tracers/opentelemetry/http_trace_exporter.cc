
#include "http_trace_exporter.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

OpenTelemetryHttpTraceExporter::OpenTelemetryHttpTraceExporter(
  Upstream::ClusterManager& cluster_manager,
  envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig http_config)
    : cluster_manager_(cluster_manager), http_config_(http_config) {}

bool OpenTelemetryHttpTraceExporter::log(const ExportTraceServiceRequest& request) {

  std::string request_body;

  // TODO: add JSON option as well (though it may need custom serializing, see https://opentelemetry.io/docs/specs/otlp/#json-protobuf-encoding)
  if (http_config_.http_format() == envoy::config::trace::v3::OpenTelemetryConfig::HttpConfig::BINARY_PROTOBUF) {
    const auto ok = request.SerializeToString(&request_body);
    if (!ok) {
      ENVOY_LOG(warn, "Error during Span proto serialization.");
      return false;
    }
  }

  Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setPath(http_config_.collector_path());
  message->headers().setHost(http_config_.collector_hostname());
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Protobuf);
  message->body().add(request_body);

  const auto thread_local_cluster = cluster_manager_.getThreadLocalCluster(http_config_.http_uri().cluster());
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(warn, "Thread local cluster not found for collector.");
    return false;
  }

  std::chrono::milliseconds timeout =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::nanoseconds(http_config_.http_uri().timeout().nanos()));
  Http::AsyncClient::Request* http_request =
    thread_local_cluster->httpAsyncClient().send(
          std::move(message), *this,
          Http::AsyncClient::RequestOptions().setTimeout(timeout));

  return http_request;
}

void OpenTelemetryHttpTraceExporter::onSuccess(const Http::AsyncClient::Request&,
                                 Http::ResponseMessagePtr&& message) {
  const auto response_code = message->headers().Status()->value().getStringView();
  if (response_code != "200") {
    ENVOY_LOG(warn, "response code: {}", response_code);
  }
}

void OpenTelemetryHttpTraceExporter::onFailure(const Http::AsyncClient::Request&,
                                 Http::AsyncClient::FailureReason) {
  ENVOY_LOG(debug, "Request failed.");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
