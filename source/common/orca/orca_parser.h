#pragma once

#include "envoy/http/header_map.h"

#include "absl/status/statusor.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Orca {

// Headers used to send ORCA load metrics from the backend.
static constexpr char kEndpointLoadMetricsHeader[] = "endpoint-load-metrics";
static constexpr char kEndpointLoadMetricsHeaderBin[] = "endpoint-load-metrics-bin";
static constexpr char kEndpointLoadMetricsHeaderJson[] = "endpoint-load-metrics-json";
// The following fields are the names of the metrics tracked in the ORCA load
// report proto.
static constexpr char kApplicationUtilizationField[] = "application_utilization";
static constexpr char kCpuUtilizationField[] = "cpu_utilization";
static constexpr char kMemUtilizationField[] = "mem_utilization";
static constexpr char kEpsField[] = "eps";
static constexpr char kRpsFractionalField[] = "rps_fractional";
static constexpr char kNamedMetricsFieldPrefix[] = "named_metrics.";

// Parses ORCA load metrics from a header map into an OrcaLoadReport proto.
// Supports native HTTP, JSON and serialized binary formats.
absl::StatusOr<xds::data::orca::v3::OrcaLoadReport>
parseOrcaLoadReportHeaders(const Envoy::Http::HeaderMap& headers);
} // namespace Orca
} // namespace Envoy
