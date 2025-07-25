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

using OnLoadReportMetricFn =
    std::function<void(absl::string_view metric_name, double metric_value)>;

void scanOrcaLoadReportMetricsMap(const Protobuf::Map<std::string, double>& metrics_map,
                                  absl::string_view metric_name,
                                  absl::string_view metric_name_prefix,
                                  OnLoadReportMetricFn on_load_report_metric) {
  absl::string_view metric_name_without_prefix = metric_name.substr(metric_name_prefix.size());
  // If the metric name is "*", report all metrics from the map.
  if (metric_name_without_prefix == "*") {
    for (const auto& [key, value] : metrics_map) {
      on_load_report_metric(absl::StrCat(metric_name_prefix, key), value);
    }
  } else {
    // Report the metric if it exists in the map.
    const auto metric_it = metrics_map.find(metric_name_without_prefix);
    if (metric_it != metrics_map.end()) {
      on_load_report_metric(metric_name, metric_it->second);
    }
  }
}

void scanOrcaLoadReport(const LrsReportMetricNames& metric_names,
                        const xds::data::orca::v3::OrcaLoadReport& report,
                        OnLoadReportMetricFn on_load_report_metric) {
  // TODO(efimki): Use InlineMap to speed up this loop.
  for (const std::string& metric_name : metric_names) {
    if (metric_name == kCpuUtilizationField) {
      on_load_report_metric(metric_name, report.cpu_utilization());
    } else if (metric_name == kMemUtilizationField) {
      on_load_report_metric(metric_name, report.mem_utilization());
    } else if (metric_name == kApplicationUtilizationField) {
      on_load_report_metric(metric_name, report.application_utilization());
    } else if (metric_name == kEpsField) {
      on_load_report_metric(metric_name, report.eps());
    } else if (metric_name == kRpsFractionalField) {
      on_load_report_metric(metric_name, report.rps_fractional());
    } else if (absl::StartsWith(metric_name, kNamedMetricsFieldPrefix)) {
      scanOrcaLoadReportMetricsMap(report.named_metrics(), metric_name, kNamedMetricsFieldPrefix,
                                   on_load_report_metric);
    } else if (absl::StartsWith(metric_name, kUtilizationFieldPrefix)) {
      scanOrcaLoadReportMetricsMap(report.utilization(), metric_name, kUtilizationFieldPrefix,
                                   on_load_report_metric);
    } else if (absl::StartsWith(metric_name, kRequestCostFieldPrefix)) {
      scanOrcaLoadReportMetricsMap(report.request_cost(), metric_name, kRequestCostFieldPrefix,
                                   on_load_report_metric);
    }
  }
}

} // namespace

void addOrcaLoadReportToLoadMetricStats(const LrsReportMetricNames& metric_names,
                                        const xds::data::orca::v3::OrcaLoadReport& report,
                                        Upstream::LoadMetricStats& stats) {
  scanOrcaLoadReport(metric_names, report,
                     [&stats](absl::string_view metric_name, double metric_value) {
                       stats.add(metric_name, metric_value);
                     });
}

double getMaxUtilization(const LrsReportMetricNames& metric_names,
                         const xds::data::orca::v3::OrcaLoadReport& report) {
  double max_utilization = 0;
  scanOrcaLoadReport(metric_names, report,
                     [&max_utilization](absl::string_view, double metric_value) {
                       max_utilization = std::max<double>(max_utilization, metric_value);
                     });
  return max_utilization;
}

} // namespace Orca
} // namespace Envoy
