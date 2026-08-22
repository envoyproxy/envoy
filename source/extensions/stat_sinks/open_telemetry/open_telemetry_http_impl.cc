#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_http_impl.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/open_telemetry/otlp_log_utils.h"
#include "source/extensions/compression/gzip/compressor/zlib_compressor_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

OpenTelemetryHttpMetricsExporter::OpenTelemetryHttpMetricsExporter(
    Upstream::ClusterManager& cluster_manager,
    const envoy::config::core::v3::HttpService& http_service,
    Server::Configuration::ServerFactoryContext& server_context,
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::Compression compression)
    : cluster_manager_(cluster_manager), http_service_(http_service), compression_(compression),
      headers_applicator_(
          Http::HttpServiceHeadersApplicator::createOrThrow(http_service, server_context)) {}

namespace {
// Gzip compression parameters. These mirror the defaults used by the gzip compressor extension.
// The gzip header value (16) is OR'ed into the window bits so the deflate stream is wrapped with a
// gzip header and trailer, which is what ``Content-Encoding: gzip`` requires.
constexpr int64_t GzipWindowBits = 12;
constexpr int64_t GzipHeaderValue = 16;
constexpr uint64_t GzipMemoryLevel = 5;
} // namespace

bool OpenTelemetryHttpMetricsExporter::compressBody(std::string& body) const {
  // Gzip is the only compression scheme required by the OTLP/HTTP specification and the only
  // value currently supported by this sink.
  Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl compressor;
  compressor.init(
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
      Extensions::Compression::Gzip::Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
      GzipWindowBits | GzipHeaderValue, GzipMemoryLevel);

  Buffer::OwnedImpl buffer;
  buffer.add(body);
  compressor.compress(buffer, Envoy::Compression::Compressor::State::Finish);
  body = buffer.toString();
  return true;
}

void OpenTelemetryHttpMetricsExporter::send(MetricsExportRequestPtr&& metrics) {
  std::string request_body;
  const auto ok = metrics->SerializeToString(&request_body);
  if (!ok) {
    ENVOY_LOG(warn, "Error while serializing the binary proto ExportMetricsServiceRequest.");
    return;
  }

  const auto thread_local_cluster =
      cluster_manager_.getThreadLocalCluster(http_service_.http_uri().cluster());
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(error, "OTLP HTTP metrics exporter failed: [cluster = {}] is not configured",
              http_service_.http_uri().cluster());
    return;
  }

  Http::RequestMessagePtr message = Http::Utility::prepareHeaders(http_service_.http_uri());

  // The request follows the OTLP HTTP specification:
  // https://github.com/open-telemetry/opentelemetry-proto/blob/v1.9.0/docs/specification.md#otlphttp
  message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
  message->headers().setReferenceContentType(Http::Headers::get().ContentTypeValues.Protobuf);

  // User-Agent header follows the OTLP specification.
  message->headers().setReferenceUserAgent(AccessLoggers::OpenTelemetry::getOtlpUserAgentHeader());

  // Optionally compress the serialized payload before sending. When enabled, set the
  // Content-Encoding header so the OTLP/HTTP collector knows how to decode the body.
  if (compression_ == envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::GZIP &&
      compressBody(request_body)) {
    message->headers().setReference(Http::CustomHeaders::get().ContentEncoding,
                                    Http::CustomHeaders::get().ContentEncodingValues.Gzip);
  }

  // Add custom headers from config.
  headers_applicator_->apply(message->headers());
  message->body().add(request_body);

  const auto options =
      Http::AsyncClient::RequestOptions()
          .setTimeout(std::chrono::milliseconds(
              DurationUtil::durationToMilliseconds(http_service_.http_uri().timeout())))
          .setDiscardResponseBody(true);

  Http::AsyncClient::Request* in_flight_request =
      thread_local_cluster->httpAsyncClient().send(std::move(message), *this, options);

  if (in_flight_request != nullptr) {
    active_requests_.add(*in_flight_request);
  }
}

void OpenTelemetryHttpMetricsExporter::onSuccess(const Http::AsyncClient::Request& request,
                                                 Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  const auto response_code = Http::Utility::getResponseStatus(http_response->headers());
  if (response_code != enumToInt(Http::Code::OK)) {
    ENVOY_LOG(error,
              "OTLP HTTP metrics exporter received a non-success status code: {} while "
              "exporting the OTLP message",
              response_code);
  }
}

void OpenTelemetryHttpMetricsExporter::onFailure(const Http::AsyncClient::Request& request,
                                                 Http::AsyncClient::FailureReason reason) {
  active_requests_.remove(request);
  ENVOY_LOG(warn, "OTLP HTTP metrics export request failed. Failure reason: {}", enumToInt(reason));
}

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
