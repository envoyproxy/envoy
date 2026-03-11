#pragma once

#include <memory>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

using MetricsExportRequest =
    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using MetricsExportRequestPtr = std::unique_ptr<MetricsExportRequest>;

class RequestSplitter {
public:
  /**
   * Splits the given resource metrics into multiple export requests based on configured limits.
   * When max_dp is reached, the subsequent data points are split into a new request.
   * If max_dp is 0, the corresponding limit is disabled.
   */
  static void chunkRequests(
      Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics>& rm_list,
      const uint32_t max_dp, const std::function<void(MetricsExportRequestPtr)>& send_callback);
};

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
