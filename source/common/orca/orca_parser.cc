#include "source/common/orca/orca_parser.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/http/header_map.h"

#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"
#include "source/common/http/header_utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

using ::Envoy::Http::HeaderMap;
using xds::data::orca::v3::OrcaLoadReport;

namespace Envoy {
namespace Orca {

namespace {

absl::Status tryCopyNamedMetricToOrcaLoadReport(absl::string_view metric_name, double metric_value,
                                                OrcaLoadReport& orca_load_report) {
  if (metric_name.empty()) {
    return absl::InvalidArgumentError("named metric key is empty.");
  }

  orca_load_report.mutable_named_metrics()->insert({std::string(metric_name), metric_value});
  return absl::OkStatus();
}

std::vector<absl::string_view> parseCommaDelimitedHeader(const HeaderMap::GetResult& entry) {
  std::vector<absl::string_view> values;
  for (size_t i = 0; i < entry.size(); ++i) {
    std::vector<absl::string_view> tokens =
        Envoy::Http::HeaderUtility::parseCommaDelimitedHeader(entry[i]->value().getStringView());
    values.insert(values.end(), tokens.begin(), tokens.end());
  }
  return values;
}

absl::Status tryCopyMetricToOrcaLoadReport(absl::string_view metric_name,
                                           absl::string_view metric_value,
                                           OrcaLoadReport& orca_load_report) {
  if (metric_name.empty()) {
    return absl::InvalidArgumentError("metric names cannot be empty strings");
  }

  if (metric_value.empty()) {
    return absl::InvalidArgumentError("metric values cannot be empty strings");
  }

  double value;
  if (!absl::SimpleAtod(metric_value, &value)) {
    return absl::InvalidArgumentError(fmt::format(
        "unable to parse custom backend load metric value({}): {}", metric_name, metric_value));
  }

  if (std::isnan(value)) {
    return absl::InvalidArgumentError(
        fmt::format("custom backend load metric value({}) cannot be NaN.", metric_name));
  }

  if (std::isinf(value)) {
    return absl::InvalidArgumentError(
        fmt::format("custom backend load metric value({}) cannot be infinity.", metric_name));
  }

  if (absl::StartsWith(metric_name, kNamedMetricsFieldPrefix)) {
    auto metric_name_without_prefix = absl::StripPrefix(metric_name, kNamedMetricsFieldPrefix);
    return tryCopyNamedMetricToOrcaLoadReport(metric_name_without_prefix, value, orca_load_report);
  }

  if (metric_name == kCpuUtilizationField) {
    orca_load_report.set_cpu_utilization(value);
  } else if (metric_name == kMemUtilizationField) {
    orca_load_report.set_mem_utilization(value);
  } else if (metric_name == kApplicationUtilizationField) {
    orca_load_report.set_application_utilization(value);
  } else if (metric_name == kEpsField) {
    orca_load_report.set_eps(value);
  } else if (metric_name == kRpsFractionalField) {
    orca_load_report.set_rps_fractional(value);
  } else {
    return absl::InvalidArgumentError(absl::StrCat("unsupported metric name: ", metric_name));
  }
  return absl::OkStatus();
}

absl::Status tryParseNativeHttpEncoded(const HeaderMap::GetResult& header,
                                       OrcaLoadReport& orca_load_report) {
  if (header.empty()) {
    return absl::InvalidArgumentError("header is empty.");
  }

  const std::vector<absl::string_view> values = parseCommaDelimitedHeader(header);

  // Check for duplicate metric names here because OrcaLoadReport fields are not
  // marked as optional and therefore don't differentiate between unset and
  // default values.
  absl::flat_hash_set<absl::string_view> metric_names;
  for (const auto value : values) {
    std::pair<absl::string_view, absl::string_view> entry =
        absl::StrSplit(value, absl::MaxSplits(':', 1), absl::SkipWhitespace());
    if (metric_names.contains(entry.first)) {
      return absl::AlreadyExistsError(
          absl::StrCat(kEndpointLoadMetricsHeader, " contains duplicate metric: ", entry.first));
    }
    RETURN_IF_NOT_OK(tryCopyMetricToOrcaLoadReport(entry.first, entry.second, orca_load_report));
    metric_names.insert(entry.first);
  }
  return absl::OkStatus();
}

} // namespace

absl::StatusOr<OrcaLoadReport> parseOrcaLoadReportHeaders(const HeaderMap& headers) {
  OrcaLoadReport load_report;

  const auto load_metrics_native_http =
      headers.get(Envoy::Http::LowerCaseString(kEndpointLoadMetricsHeader));
  const auto load_metrics_json =
      headers.get(Envoy::Http::LowerCaseString(kEndpointLoadMetricsHeaderJson));
  const auto load_metrics_bin =
      headers.get(Envoy::Http::LowerCaseString(kEndpointLoadMetricsHeaderBin));

  if (load_metrics_native_http.empty() && load_metrics_json.empty() && load_metrics_bin.empty()) {
    return absl::NotFoundError("no ORCA data sent from the backend");
  }

  if ((!load_metrics_native_http.empty() && !load_metrics_json.empty()) ||
      (!load_metrics_native_http.empty() && !load_metrics_bin.empty()) ||
      (!load_metrics_json.empty() && !load_metrics_bin.empty())) {
    // If more than one ORCA header format is found, we will be
    // unable to determine which header to use.
    return absl::InvalidArgumentError("more than one ORCA header found");
  }

  // Native HTTP format.
  if (!load_metrics_native_http.empty()) {
    RETURN_IF_NOT_OK(tryParseNativeHttpEncoded(load_metrics_native_http, load_report));
  }

  // JSON format.
  if (!load_metrics_json.empty()) {
#if defined(ENVOY_ENABLE_FULL_PROTOS) && defined(ENVOY_ENABLE_YAML)
    bool has_unknown_field = false;
    std::string json_string = std::string(load_metrics_json[0]->value().getStringView());
    RETURN_IF_ERROR(
        Envoy::MessageUtil::loadFromJsonNoThrow(json_string, load_report, has_unknown_field));
#else
    IS_ENVOY_BUG("JSON formatted ORCA header support not implemented for this build");
#endif // !ENVOY_ENABLE_FULL_PROTOS || !ENVOY_ENABLE_YAML
  }

  // Binary protobuf format.
  if (!load_metrics_bin.empty()) {
    auto header_value = load_metrics_bin[0]->value().getStringView();
    std::string decoded_value = Envoy::Base64::decode(header_value);
    if (!load_report.ParseFromString(decoded_value)) {
      return absl::InvalidArgumentError(
          fmt::format("unable to parse binaryheader to OrcaLoadReport: {}", header_value));
    }
  }
  return load_report;
}

} // namespace Orca
} // namespace Envoy
