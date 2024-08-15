#include "source/common/orca/orca_load_metrics.h"

#include <string>

#include "source/common/orca/orca_parser.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Orca {
namespace {
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
} // namespace

void addOrcaNamedMetricToLoadMetricStats(const Protobuf::Map<std::string, double>& metrics_map,
                                         const absl::string_view metric_name,
                                         const absl::string_view metric_name_prefix,
                                         Upstream::LoadMetricStats& stats) {
  absl::string_view metric_name_without_prefix = absl::StripPrefix(metric_name, metric_name_prefix);
  // If the metric name is "*", add all metrics from the map.
  if (metric_name_without_prefix == "*") {
    for (const auto& [key, value] : metrics_map) {
      stats.add(absl::StrCat(metric_name_prefix, key), value);
    }
  } else {
    // Add the metric if it exists in the map.
    const auto metric_it = metrics_map.find(metric_name_without_prefix);
    if (metric_it != metrics_map.end()) {
      stats.add(metric_name, metric_it->second);
    }
  }
}

void addOrcaLoadReportToLoadMetricStats(const LrsReportMetricNames& metric_names,
                                        const xds::data::orca::v3::OrcaLoadReport& report,
                                        Upstream::LoadMetricStats& stats) {
  // TODO(efimki): Use InlineMap to speed up this loop.
  for (const std::string& metric_name : metric_names) {
    if (metric_name == kCpuUtilizationField) {
      stats.add(metric_name, report.cpu_utilization());
    } else if (metric_name == kMemUtilizationField) {
      stats.add(metric_name, report.mem_utilization());
    } else if (metric_name == kApplicationUtilizationField) {
      stats.add(metric_name, report.application_utilization());
    } else if (metric_name == kEpsField) {
      stats.add(metric_name, report.eps());
    } else if (metric_name == kRpsFractionalField) {
      stats.add(metric_name, report.rps_fractional());
    } else if (absl::StartsWith(metric_name, kNamedMetricsFieldPrefix)) {
      addOrcaNamedMetricToLoadMetricStats(report.named_metrics(), metric_name,
                                          kNamedMetricsFieldPrefix, stats);
    } else if (absl::StartsWith(metric_name, kUtilizationFieldPrefix)) {
      addOrcaNamedMetricToLoadMetricStats(report.utilization(), metric_name,
                                          kUtilizationFieldPrefix, stats);
    } else if (absl::StartsWith(metric_name, kRequestCostFieldPrefix)) {
      addOrcaNamedMetricToLoadMetricStats(report.request_cost(), metric_name,
                                          kRequestCostFieldPrefix, stats);
    }
  }
}

} // namespace Orca
} // namespace Envoy
