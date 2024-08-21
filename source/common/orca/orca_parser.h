#pragma once

#include "envoy/http/header_map.h"

#include "absl/status/statusor.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Orca {

// Header used to send ORCA load metrics from the backend.
static constexpr absl::string_view kEndpointLoadMetricsHeaderBin = "endpoint-load-metrics-bin";

// Parses ORCA load metrics from a header map into an OrcaLoadReport proto.
// Supports serialized binary formats.
absl::StatusOr<xds::data::orca::v3::OrcaLoadReport>
parseOrcaLoadReportHeaders(const Envoy::Http::HeaderMap& headers);
} // namespace Orca
} // namespace Envoy
