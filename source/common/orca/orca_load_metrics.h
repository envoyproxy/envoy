#pragma once

#include "envoy/upstream/host_description.h"

#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Orca {

// List of metric names to report to the LRS.
typedef std::vector<std::string> LrsReportMetricNames;

// Adds metrics with `metric_names` from the `report` to the `stats`.
void addOrcaLoadReportToLoadMetricStats(const LrsReportMetricNames& metric_names,
                                        const xds::data::orca::v3::OrcaLoadReport& report,
                                        Upstream::LoadMetricStats& stats);

// Returns the maximum value of metrics with `metric_names` in the `report`.
double getMaxUtilization(const LrsReportMetricNames& metric_names,
                         const xds::data::orca::v3::OrcaLoadReport& report);

} // namespace Orca
} // namespace Envoy
