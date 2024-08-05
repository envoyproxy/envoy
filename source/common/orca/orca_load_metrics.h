#pragma once

#ifndef THIRD_PARTY_ENVOY_SRC_SOURCE_COMMON_ORCA_ORCA_LOAD_METRICS_H_
#define THIRD_PARTY_ENVOY_SRC_SOURCE_COMMON_ORCA_ORCA_LOAD_METRICS_H_

#include "envoy/upstream/host_description.h"

#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Orca {

// List of metric names to report to the LRS.
typedef std::vector<std::string> LrsReportMetricNames;

// The following fields are the names of the metrics tracked in the ORCA load
// report proto.
static constexpr absl::string_view kApplicationUtilizationField = "application_utilization";
static constexpr absl::string_view kCpuUtilizationField = "cpu_utilization";
static constexpr absl::string_view kMemUtilizationField = "mem_utilization";
static constexpr absl::string_view kEpsField = "eps";
static constexpr absl::string_view kRpsFractionalField = "rps_fractional";
static constexpr absl::string_view kNamedMetricsFieldPrefix = "named_metrics.";
static constexpr absl::string_view kRequestCostFieldPrefix = "request_cost.";
static constexpr absl::string_view kUtilizationFieldPrefix = "utilization.";

void addOrcaLoadReportToLoadMetricStats(const OptRef<const LrsReportMetricNames>& metric_names,
                                        const xds::data::orca::v3::OrcaLoadReport& report,
                                        Upstream::LoadMetricStats& stats);

} // namespace Orca
} // namespace Envoy

#endif // THIRD_PARTY_ENVOY_SRC_SOURCE_COMMON_ORCA_ORCA_LOAD_METRICS_H_
