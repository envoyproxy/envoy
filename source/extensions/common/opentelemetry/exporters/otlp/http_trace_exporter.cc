#include "source/extensions/common/opentelemetry/exporters/otlp/http_trace_exporter.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/environment.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Exporters {
namespace Otlp {

OtlpHttpTraceExporter::OtlpHttpTraceExporter(
    Upstream::ClusterManager& cluster_manager,
    const envoy::config::core::v3::HttpService& http_service,
    std::shared_ptr<const Http::HttpServiceHeadersApplicator> headers_applicator)
    : cluster_manager_(cluster_manager), http_service_(http_service),
      headers_applicator_(std::move(headers_applicator)) {}

bool OtlpHttpTraceExporter::log(
    const opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest& request) {
  std::string request_body;

  const auto ok = request.SerializeToString(&request_body);
  if (!ok) {
    ENVOY_LOG(
        warn,
        "Error while serializing the binary proto ExportTraceServiceRequest."); // LCOV_EXCL_LINE
    return false;                                                               // LCOV_EXCL_LINE
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
  message->headers().setReferenceUserAgent(GetUserAgent());

  // Add all custom headers to the request (static values set once; formatted values
  // re-evaluated now so that runtime updates, e.g. SDS rotation, are reflected).
  headers_applicator_->apply(message->headers());

  message->body().add(request_body);

  const auto options =
      Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(http_service_.http_uri().timeout())))
          .setDiscardResponseBody(true);

  Http::AsyncClient::Request* in_flight_request =
      thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);

  OtlpTraceExporter::logExportedSpans(request);

  if (in_flight_request == nullptr) {
    return false;
  }

  active_requests_.add(*in_flight_request);
  return true;
}

void OtlpHttpTraceExporter::onSuccess(const Http::AsyncClient::Request& request,
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

void OtlpHttpTraceExporter::onFailure(const Http::AsyncClient::Request& request,
                                      Http::AsyncClient::FailureReason reason) {
  active_requests_.remove(request);
  ENVOY_LOG(debug, "The OTLP export request failed. Reason {}", enumToInt(reason));
}

} // namespace Otlp
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
