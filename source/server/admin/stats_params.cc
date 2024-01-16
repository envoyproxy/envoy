#include "source/server/admin/stats_params.h"

namespace Envoy {
namespace Server {

Http::Code StatsParams::parse(absl::string_view url, Buffer::Instance& response) {
  query_ = Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(url);
  used_only_ = query_.getFirstValue("usedonly").has_value();
  pretty_ = query_.getFirstValue("pretty").has_value();
  prometheus_text_readouts_ = query_.getFirstValue("text_readouts").has_value();

  auto filter_val = query_.getFirstValue("filter");
  if (filter_val.has_value() && !filter_val.value().empty()) {
    filter_string_ = filter_val.value();
    re2::RE2::Options options;
    options.set_log_errors(false);
    re2_filter_ = std::make_shared<re2::RE2>(filter_string_, options);
    if (!re2_filter_->ok()) {
      response.add("Invalid re2 regex");
      return Http::Code::BadRequest;
    }
  }

  absl::Status status = Utility::histogramBucketsParam(query_, histogram_buckets_mode_);
  if (!status.ok()) {
    response.add(status.message());
    return Http::Code::BadRequest;
  }

  auto parse_type = [](absl::string_view str, StatsType& type) {
    if (str == StatLabels::Gauges) {
      type = StatsType::Gauges;
    } else if (str == StatLabels::Counters) {
      type = StatsType::Counters;
    } else if (str == StatLabels::Histograms) {
      type = StatsType::Histograms;
    } else if (str == StatLabels::TextReadouts) {
      type = StatsType::TextReadouts;
    } else if (str == StatLabels::All) {
      type = StatsType::All;
    } else {
      return false;
    }
    return true;
  };

  auto type_val = query_.getFirstValue("type");
  if (type_val.has_value() && !type_val.value().empty() && !parse_type(type_val.value(), type_)) {
    response.add("invalid &type= param");
    return Http::Code::BadRequest;
  }

  const absl::optional<std::string> hidden_value = query_.getFirstValue("hidden");
  if (hidden_value.has_value() && !hidden_value.value().empty()) {
    if (hidden_value.value() == "include") {
      hidden_ = HiddenFlag::Include;
    } else if (hidden_value.value() == "only") {
      hidden_ = HiddenFlag::ShowOnly;
    } else if (hidden_value.value() == "exclude") {
      hidden_ = HiddenFlag::Exclude;
    } else {
      response.add("usage: /stats?hidden=(include|only|exclude)\n\n");
      return Http::Code::BadRequest;
    }
  }

  const absl::optional<std::string> format_value = Utility::formatParam(query_);
  if (format_value.has_value() && !format_value.value().empty()) {
    if (format_value.value() == "prometheus") {
      format_ = StatsFormat::Prometheus;
    } else if (format_value.value() == "json") {
      format_ = StatsFormat::Json;
    } else if (format_value.value() == "text") {
      format_ = StatsFormat::Text;
    } else if (format_value.value() == "html") {
#ifdef ENVOY_ADMIN_HTML
      format_ = StatsFormat::Html;
#else
      response.add("HTML output was disabled by building with --define=admin_html=disabled");
      return Http::Code::BadRequest;
#endif
    } else if (format_value.value() == "active-html") {
#ifdef ENVOY_ADMIN_HTML
      format_ = StatsFormat::ActiveHtml;
#else
      response.add("Active HTML output was disabled by building with --define=admin_html=disabled");
      return Http::Code::BadRequest;
#endif
    } else {
      response.add("usage: /stats?format=(html|active-html|json|prometheus|text)\n\n");
      return Http::Code::BadRequest;
    }
  }

  return Http::Code::OK;
}

absl::string_view StatsParams::typeToString(StatsType type) {
  absl::string_view ret;
  switch (type) {
  case StatsType::TextReadouts:
    ret = StatLabels::TextReadouts;
    break;
  case StatsType::Counters:
    ret = StatLabels::Counters;
    break;
  case StatsType::Gauges:
    ret = StatLabels::Gauges;
    break;
  case StatsType::Histograms:
    ret = StatLabels::Histograms;
    break;
  case StatsType::All:
    ret = StatLabels::All;
    break;
  }
  return ret;
}

} // namespace Server
} // namespace Envoy
