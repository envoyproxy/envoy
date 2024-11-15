#include "source/extensions/tracers/opentelemetry/http_trace_exporter.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/tracers/opentelemetry/otlp_utils.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

OpenTelemetryHttpTraceExporter::OpenTelemetryHttpTraceExporter(
    Upstream::ClusterManager& cluster_manager,
    const envoy::config::core::v3::HttpService& http_service)
    : cluster_manager_(cluster_manager), http_service_(http_service) {

  // Prepare and store headers to be used later on each export request
  for (const auto& header_value_option : http_service_.request_headers_to_add()) {
    parsed_headers_to_add_.push_back({Http::LowerCaseString(header_value_option.header().key()),
                                      header_value_option.header().value()});
  }
}

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

  // The request follows the OTLP HTTP specification:
  // https://github.com/open-telemetry/opentelemetry-proto/blob/v1.0.0/docs/specification.md#otlphttp.
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Protobuf);

  // User-Agent header follows the OTLP specification:
  // https://github.com/open-telemetry/opentelemetry-specification/blob/v1.30.0/specification/protocol/exporter.md#user-agent
  message->headers().setReferenceUserAgent(OtlpUtils::getOtlpUserAgentHeader());

  // Add all custom headers to the request.
  for (const auto& header_pair : parsed_headers_to_add_) {
    message->headers().setReference(header_pair.first, header_pair.second);
  }
  message->body().add(request_body);

  const auto options =
      Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(http_service_.http_uri().timeout())))
          .setDiscardResponseBody(true);

  Http::AsyncClient::Request* in_flight_request =
      thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);

  if (in_flight_request == nullptr) {
    return false;
  }

  active_requests_.add(*in_flight_request);
  return true;
}

void OpenTelemetryHttpTraceExporter::onSuccess(const Http::AsyncClient::Request& request,
                                               Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  const auto response_code = Http::Utility::getResponseStatus(http_response->headers());
  if (response_code != enumToInt(Http::Code::OK)) {
    ENVOY_LOG(error,
              "OTLP HTTP exporter received a non-success status code: {} while exporting the OTLP "
              "message",
              response_code);
  }
}

void OpenTelemetryHttpTraceExporter::onFailure(const Http::AsyncClient::Request& request,
                                               Http::AsyncClient::FailureReason reason) {
  active_requests_.remove(request);
  ENVOY_LOG(debug, "The OTLP export request failed. Reason {}", enumToInt(reason));
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
