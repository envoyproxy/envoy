#pragma once
// NOLINT(namespace-envoy)

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Sdk {
namespace Metrics {

struct Constants {
  static constexpr absl::string_view kMetricsServiceExportMethod =
      "opentelemetry.proto.collector.metrics.v1.MetricsService.Export";

  static constexpr absl::string_view kDefaultOtlpMetricsEndpoint = "/v1/metrics";
};

} // namespace Metrics
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
