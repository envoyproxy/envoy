#include "source/server/admin/stats_params.h"

namespace Envoy {
namespace Server {

/*bool StatsParams::shouldShowMetric(const Stats::Metric& metric) const {
  if (used_only_ && !metric.used()) {
    return false;
  }
  if (filter_.has_value()) {
    std::string name = metric.name();
    if (!std::regex_search(name, filter_.value())) {
      return false;
    }
  }
  return true;
  }*/

Http::Code StatsParams::parse(absl::string_view url, Buffer::Instance& response) {
  query_ = Http::Utility::parseAndDecodeQueryString(url);
  used_only_ = query_.find("usedonly") != query_.end();
  pretty_ = query_.find("pretty") != query_.end();
  prometheus_text_readouts_ = query_.find("text_readouts") != query_.end();
  if (!Utility::filterParam(query_, response, filter_)) {
    return Http::Code::BadRequest;
  }
  if (filter_.has_value()) {
    filter_string_ = query_.find("filter")->second;
  }

  absl::Status status = Utility::histogramBucketsParam(query_, histogram_buckets_mode_);
  if (!status.ok()) {
    response.add(status.message());
    return Http::Code::BadRequest;
  }

  auto parse_type = [](absl::string_view str, StatsType& type) {
    if (str == Labels::Gauges) {
      type = StatsType::Gauges;
    } else if (str == Labels::Counters) {
      type = StatsType::Counters;
    } else if (str == Labels::Histograms) {
      type = StatsType::Histograms;
    } else if (str == Labels::TextReadouts) {
      type = StatsType::TextReadouts;
    } else if (str == Labels::All) {
      type = StatsType::All;
    } else {
      return false;
    }
    return true;
  };

  auto type_iter = query_.find("type");
  if (type_iter != query_.end() && !parse_type(type_iter->second, type_)) {
    response.add("invalid &type= param");
    return Http::Code::BadRequest;
  }

  const absl::optional<std::string> format_value = Utility::formatParam(query_);
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      format_ = StatsFormat::Prometheus;
    } else if (format_value.value() == "json") {
      format_ = StatsFormat::Json;
    } else if (format_value.value() == "text") {
      format_ = StatsFormat::Text;
    } else if (format_value.value() == "html") {
      format_ = StatsFormat::Html;
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n\n");
      return Http::Code::BadRequest;
    }
  }

  return Http::Code::OK;
}

absl::string_view StatsParams::typeToString(StatsType type) {
  absl::string_view ret;
  switch (type) {
  case StatsType::TextReadouts:
    ret = Labels::TextReadouts;
    break;
  case StatsType::Counters:
    ret = Labels::Counters;
    break;
  case StatsType::Gauges:
    ret = Labels::Gauges;
    break;
  case StatsType::Histograms:
    ret = Labels::Histograms;
    break;
  case StatsType::All:
    ret = Labels::All;
    break;
  }
  return ret;
}

} // namespace Server
} // namespace Envoy
