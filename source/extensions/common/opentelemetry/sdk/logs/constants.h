#pragma once
// NOLINT(namespace-envoy)

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace OpenTelemetry {
namespace Sdk {
namespace Logs {

struct Constants {
  static constexpr absl::string_view kLogsServiceExportMethod =
      "opentelemetry.proto.collector.logs.v1.LogsService.Export";

  static constexpr absl::string_view kDefaultOtlpLogsEndpoint = "/v1/logs";
};

} // namespace Logs
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Extensions
} // namespace Envoy
