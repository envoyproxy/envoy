#pragma once

#include "envoy/config/core/v3/http_service.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/http/async_client_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/opentelemetry/exporters/otlp/trace_exporter.h"

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Exporters {
namespace OTLP {

/**
 * Exporter for OTLP traces over HTTP.
 */
class OpenTelemetryHttpTraceExporter : public OpenTelemetryTraceExporter,
                                       public Http::AsyncClient::Callbacks {
public:
  OpenTelemetryHttpTraceExporter(Upstream::ClusterManager& cluster_manager,
                                 const envoy::config::core::v3::HttpService& http_service);

  bool log(const ExportTraceServiceRequest& request) override;

  // Http::AsyncClient::Callbacks.
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}

private:
  Upstream::ClusterManager& cluster_manager_;
  envoy::config::core::v3::HttpService http_service_;
  // Track active HTTP requests to be able to cancel them on destruction.
  Http::AsyncClientRequestTracker active_requests_;
  std::vector<std::pair<const Http::LowerCaseString, const std::string>> parsed_headers_to_add_;
};

} // namespace OTLP
} // namespace Exporters
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
