#pragma once

#include "envoy/http/header_map.h"

#include "absl/status/statusor.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Orca {

// Headers used to send ORCA load metrics from the backend.
static constexpr absl::string_view kEndpointLoadMetricsHeader = "endpoint-load-metrics";
static constexpr absl::string_view kEndpointLoadMetricsHeaderBin = "endpoint-load-metrics-bin";
// Prefix used to determine format expected in kEndpointLoadMetricsHeader.
static constexpr absl::string_view kHeaderFormatPrefixBin = "BIN ";
static constexpr absl::string_view kHeaderFormatPrefixJson = "JSON ";
static constexpr absl::string_view kHeaderFormatPrefixText = "TEXT ";
// The following fields are the names of the metrics tracked in the ORCA load
// report proto.
static constexpr absl::string_view kApplicationUtilizationField = "application_utilization";
static constexpr absl::string_view kCpuUtilizationField = "cpu_utilization";
static constexpr absl::string_view kMemUtilizationField = "mem_utilization";
static constexpr absl::string_view kEpsField = "eps";
static constexpr absl::string_view kRpsFractionalField = "rps_fractional";
static constexpr absl::string_view kNamedMetricsFieldPrefix = "named_metrics.";

// Parses ORCA load metrics from a header map into an OrcaLoadReport proto.
// Supports native HTTP, JSON and serialized binary formats.
absl::StatusOr<xds::data::orca::v3::OrcaLoadReport>
parseOrcaLoadReportHeaders(const Envoy::Http::HeaderMap& headers);
} // namespace Orca
} // namespace Envoy
