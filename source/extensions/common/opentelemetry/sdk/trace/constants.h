#pragma once
// NOLINT(namespace-envoy)

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Sdk {
namespace Trace {

struct Constants {
  static constexpr absl::string_view kTraceServiceExportMethod =
      "opentelemetry.proto.collector.trace.v1.TraceService.Export";

  static constexpr absl::string_view kDefaultOtlpTracesEndpoint = "/v1/traces";
};

} // namespace Trace
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
