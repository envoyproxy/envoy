#pragma once

#include <memory>

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace OpenTelemetry {
namespace Sdk {
namespace Metrics {

/**
 * OpenTelemetry metrics signal type aliases.
 *
 * Origin: All types are derived from official OpenTelemetry C++ SDK and protocol definitions
 * Reference: https://github.com/open-telemetry/opentelemetry-cpp
 */

/**
 * @brief Aggregation temporality for metrics (from OpenTelemetry specification)
 */
using AggregationTemporality = ::opentelemetry::proto::metrics::v1::AggregationTemporality;

// OTLP export request/response types for metrics (from OTLP protocol)
using ExportRequest = ::opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using ExportResponse = ::opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse;

// Smart pointer aliases for metrics export requests (Envoy convenience types)
using ExportRequestPtr = std::unique_ptr<ExportRequest>;
using ExportRequestSharedPtr = std::shared_ptr<ExportRequest>;

} // namespace Metrics
} // namespace Sdk
} // namespace OpenTelemetry
} // namespace Common
} // namespace Extensions
} // namespace Envoy
