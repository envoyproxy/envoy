#pragma once

#include "envoy/config/core/v3/http_service.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/async_client_impl.h"
#include "source/common/http/async_client_utility.h"
#include "source/common/http/http_service_headers.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

/**
 * HTTP implementation of OtlpMetricsExporter.
 * Exports OTLP metrics over HTTP following the OTLP/HTTP specification.
 */
class OpenTelemetryHttpMetricsExporter : public OtlpMetricsExporter,
                                         public Http::AsyncClient::Callbacks,
                                         public Logger::Loggable<Logger::Id::stats> {
public:
  OpenTelemetryHttpMetricsExporter(Upstream::ClusterManager& cluster_manager,
                                   const envoy::config::core::v3::HttpService& http_service,
                                   Server::Configuration::ServerFactoryContext& server_context);

  // OtlpMetricsExporter
  void send(MetricsExportRequestPtr&& metrics) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  Upstream::ClusterManager& cluster_manager_;
  envoy::config::core::v3::HttpService http_service_;
  // Track active HTTP requests to cancel them on destruction.
  Http::AsyncClientRequestTracker active_requests_;
  std::unique_ptr<Http::HttpServiceHeadersApplicator> headers_applicator_;
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
