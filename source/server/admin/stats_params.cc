#include "source/server/admin/stats_params.h"

namespace Envoy {
namespace Server {

Http::Code StatsParams::parse(absl::string_view url, Buffer::Instance& response) {
  query_ = Http::Utility::parseAndDecodeQueryString(url);
  used_only_ = query_.find("usedonly") != query_.end();
  pretty_ = query_.find("pretty") != query_.end();
  prometheus_text_readouts_ = query_.find("text_readouts") != query_.end();

  auto filter_iter = query_.find("filter");
  if (filter_iter != query_.end()) {
    filter_string_ = filter_iter->second;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.admin_stats_filter_use_re2")) {
      re2::RE2::Options options;
      options.set_log_errors(false);
      re2_filter_ = std::make_shared<re2::RE2>(filter_string_, options);
      filter_.reset();
      if (!re2_filter_->ok()) {
        response.add("Invalid re2 regex");
        return Http::Code::BadRequest;
      }
    } else {
      if (Utility::filterParam(query_, response, filter_)) {
        re2_filter_.reset();
      } else {
        return Http::Code::BadRequest;
      }
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
#ifdef ENVOY_ADMIN_HTML
      format_ = StatsFormat::Html;
#else
      response.add("HTML output was disabled by building with --define=admin_html=disabled");
      return Http::Code::BadRequest;
#endif
    } else {
      response.add("usage: /stats?format=(html|json|prometheus|text)\n\n");
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
