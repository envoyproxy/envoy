#include "server/http/stats_handler.h"

#include "common/common/empty_string.h"
#include "common/html/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

#include "server/http/utils.h"

namespace Envoy {
namespace Server {

const uint64_t RecentLookupsCapacity = 100;

namespace {
const std::regex& promRegex() { CONSTRUCT_ON_FIRST_USE(std::regex, "[^a-zA-Z0-9_]"); }
} // namespace

Http::Code StatsHandler::handlerResetCounters(absl::string_view, Http::ResponseHeaderMap&,
                                              Buffer::Instance& response, AdminStream&,
                                              Server::Instance& server) {
  for (const Stats::CounterSharedPtr& counter : server.stats().counters()) {
    counter->reset();
  }
  server.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookups(absl::string_view, Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response, AdminStream&,
                                                   Server::Instance& server) {
  Stats::SymbolTable& symbol_table = server.stats().symbolTable();
  std::string table;
  const uint64_t total =
      symbol_table.getRecentLookups([&table](absl::string_view name, uint64_t count) {
        table += fmt::format("{:8d} {}\n", count, name);
      });
  if (table.empty() && symbol_table.recentLookupCapacity() == 0) {
    table = "Lookup tracking is not enabled. Use /stats/recentlookups/enable to enable.\n";
  } else {
    response.add("   Count Lookup\n");
  }
  response.add(absl::StrCat(table, "\ntotal: ", total, "\n"));
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsClear(absl::string_view, Http::ResponseHeaderMap&,
                                                        Buffer::Instance& response, AdminStream&,
                                                        Server::Instance& server) {
  server.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsDisable(absl::string_view,
                                                          Http::ResponseHeaderMap&,
                                                          Buffer::Instance& response, AdminStream&,
                                                          Server::Instance& server) {
  server.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsEnable(absl::string_view,
                                                         Http::ResponseHeaderMap&,
                                                         Buffer::Instance& response, AdminStream&,
                                                         Server::Instance& server) {
  server.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStats(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream& admin_stream,
                                      Server::Instance& server) {
  Http::Code rc = Http::Code::OK;
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);

  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  if (!Utility::filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }

  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : server.stats().counters()) {
    if (shouldShowMetric(*counter, used_only, regex)) {
      all_stats.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : server.stats().gauges()) {
    if (shouldShowMetric(*gauge, used_only, regex)) {
      ASSERT(gauge->importMode() != Stats::Gauge::ImportMode::Uninitialized);
      all_stats.emplace(gauge->name(), gauge->value());
    }
  }

  std::map<std::string, std::string> text_readouts;
  for (const auto& text_readout : server.stats().textReadouts()) {
    if (shouldShowMetric(*text_readout, used_only, regex)) {
      text_readouts.emplace(text_readout->name(), text_readout->value());
    }
  }

  if (const auto format_value = Utility::formatParam(params)) {
    if (format_value.value() == "json") {
      response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
      response.add(
          statsAsJson(all_stats, text_readouts, server.stats().histograms(), used_only, regex));
    } else if (format_value.value() == "prometheus") {
      return handlerPrometheusStats(url, response_headers, response, admin_stream, server);
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n");
      response.add("\n");
      rc = Http::Code::NotFound;
    }
  } else { // Display plain stats if format query param is not there.
    for (const auto& text_readout : text_readouts) {
      response.add(fmt::format("{}: \"{}\"\n", text_readout.first,
                               Html::Utility::sanitize(text_readout.second)));
    }
    for (const auto& stat : all_stats) {
      response.add(fmt::format("{}: {}\n", stat.first, stat.second));
    }
    // TODO(ramaraochavali): See the comment in ThreadLocalStoreImpl::histograms() for why we use a
    // multimap here. This makes sure that duplicate histograms get output. When shared storage is
    // implemented this can be switched back to a normal map.
    std::multimap<std::string, std::string> all_histograms;
    for (const Stats::ParentHistogramSharedPtr& histogram : server.stats().histograms()) {
      if (shouldShowMetric(*histogram, used_only, regex)) {
        all_histograms.emplace(histogram->name(), histogram->quantileSummary());
      }
    }
    for (const auto& histogram : all_histograms) {
      response.add(fmt::format("{}: {}\n", histogram.first, histogram.second));
    }
  }
  return rc;
}

Http::Code StatsHandler::handlerPrometheusStats(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&,
                                                Server::Instance& server) {
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(path_and_query);
  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  if (!Utility::filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }
  PrometheusStatsFormatter::statsAsPrometheus(server.stats().counters(), server.stats().gauges(),
                                              server.stats().histograms(), response, used_only,
                                              regex);
  return Http::Code::OK;
}

std::string PrometheusStatsFormatter::sanitizeName(const std::string& name) {
  // The name must match the regex [a-zA-Z_][a-zA-Z0-9_]* as required by
  // prometheus. Refer to https://prometheus.io/docs/concepts/data_model/.
  std::string stats_name = std::regex_replace(name, promRegex(), "_");
  if (stats_name[0] >= '0' && stats_name[0] <= '9') {
    return absl::StrCat("_", stats_name);
  } else {
    return stats_name;
  }
}

std::string PrometheusStatsFormatter::formattedTags(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  buf.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}=\"{}\"", sanitizeName(tag.name_), tag.value_));
  }
  return absl::StrJoin(buf, ",");
}

std::string PrometheusStatsFormatter::metricName(const std::string& extracted_name) {
  // Add namespacing prefix to avoid conflicts, as per best practice:
  // https://prometheus.io/docs/practices/naming/#metric-names
  // Also, naming conventions on https://prometheus.io/docs/concepts/data_model/
  return sanitizeName(fmt::format("envoy_{0}", extracted_name));
}

// TODO(efimki): Add support of text readouts stats.
uint64_t PrometheusStatsFormatter::statsAsPrometheus(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms, Buffer::Instance& response,
    const bool used_only, const absl::optional<std::regex>& regex) {
  std::unordered_set<std::string> metric_type_tracker;
  for (const auto& counter : counters) {
    if (!shouldShowMetric(*counter, used_only, regex)) {
      continue;
    }

    const std::string tags = formattedTags(counter->tags());
    const std::string metric_name = metricName(counter->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} counter\n", metric_name));
    }
    response.add(fmt::format("{0}{{{1}}} {2}\n", metric_name, tags, counter->value()));
  }

  for (const auto& gauge : gauges) {
    if (!shouldShowMetric(*gauge, used_only, regex)) {
      continue;
    }

    const std::string tags = formattedTags(gauge->tags());
    const std::string metric_name = metricName(gauge->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} gauge\n", metric_name));
    }
    response.add(fmt::format("{0}{{{1}}} {2}\n", metric_name, tags, gauge->value()));
  }

  for (const auto& histogram : histograms) {
    if (!shouldShowMetric(*histogram, used_only, regex)) {
      continue;
    }

    const std::string tags = formattedTags(histogram->tags());
    const std::string hist_tags = histogram->tags().empty() ? EMPTY_STRING : (tags + ",");

    const std::string metric_name = metricName(histogram->tagExtractedName());
    if (metric_type_tracker.find(metric_name) == metric_type_tracker.end()) {
      metric_type_tracker.insert(metric_name);
      response.add(fmt::format("# TYPE {0} histogram\n", metric_name));
    }

    const Stats::HistogramStatistics& stats = histogram->cumulativeStatistics();
    const std::vector<double>& supported_buckets = stats.supportedBuckets();
    const std::vector<uint64_t>& computed_buckets = stats.computedBuckets();
    for (size_t i = 0; i < supported_buckets.size(); ++i) {
      double bucket = supported_buckets[i];
      uint64_t value = computed_buckets[i];
      // We want to print the bucket in a fixed point (non-scientific) format. The fmt library
      // doesn't have a specific modifier to format as a fixed-point value only so we use the
      // 'g' operator which prints the number in general fixed point format or scientific format
      // with precision 50 to round the number up to 32 significant digits in fixed point format
      // which should cover pretty much all cases
      response.add(fmt::format("{0}_bucket{{{1}le=\"{2:.32g}\"}} {3}\n", metric_name, hist_tags,
                               bucket, value));
    }

    response.add(fmt::format("{0}_bucket{{{1}le=\"+Inf\"}} {2}\n", metric_name, hist_tags,
                             stats.sampleCount()));
    response.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", metric_name, tags, stats.sampleSum()));
    response.add(fmt::format("{0}_count{{{1}}} {2}\n", metric_name, tags, stats.sampleCount()));
  }

  return metric_type_tracker.size();
}

std::string
StatsHandler::statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                          const std::map<std::string, std::string>& text_readouts,
                          const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                          const bool used_only, const absl::optional<std::regex> regex,
                          const bool pretty_print) {

  ProtobufWkt::Struct document;
  std::vector<ProtobufWkt::Value> stats_array;
  for (const auto& text_readout : text_readouts) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(text_readout.first);
    (*stat_obj_fields)["value"] = ValueUtil::stringValue(text_readout.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }
  for (const auto& stat : all_stats) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(stat.first);
    (*stat_obj_fields)["value"] = ValueUtil::numberValue(stat.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }

  ProtobufWkt::Struct histograms_obj;
  auto* histograms_obj_fields = histograms_obj.mutable_fields();

  ProtobufWkt::Struct histograms_obj_container;
  auto* histograms_obj_container_fields = histograms_obj_container.mutable_fields();
  std::vector<ProtobufWkt::Value> computed_quantile_array;

  bool found_used_histogram = false;
  for (const Stats::ParentHistogramSharedPtr& histogram : all_histograms) {
    if (shouldShowMetric(*histogram, used_only, regex)) {
      if (!found_used_histogram) {
        // It is not possible for the supported quantiles to differ across histograms, so it is ok
        // to send them once.
        Stats::HistogramStatisticsImpl empty_statistics;
        std::vector<ProtobufWkt::Value> supported_quantile_array;
        for (double quantile : empty_statistics.supportedQuantiles()) {
          supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
        }
        (*histograms_obj_fields)["supported_quantiles"] =
            ValueUtil::listValue(supported_quantile_array);
        found_used_histogram = true;
      }

      ProtobufWkt::Struct computed_quantile;
      auto* computed_quantile_fields = computed_quantile.mutable_fields();
      (*computed_quantile_fields)["name"] = ValueUtil::stringValue(histogram->name());

      std::vector<ProtobufWkt::Value> computed_quantile_value_array;
      for (size_t i = 0; i < histogram->intervalStatistics().supportedQuantiles().size(); ++i) {
        ProtobufWkt::Struct computed_quantile_value;
        auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
        const auto& interval = histogram->intervalStatistics().computedQuantiles()[i];
        const auto& cumulative = histogram->cumulativeStatistics().computedQuantiles()[i];
        (*computed_quantile_value_fields)["interval"] =
            std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
        (*computed_quantile_value_fields)["cumulative"] =
            std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

        computed_quantile_value_array.push_back(ValueUtil::structValue(computed_quantile_value));
      }
      (*computed_quantile_fields)["values"] = ValueUtil::listValue(computed_quantile_value_array);
      computed_quantile_array.push_back(ValueUtil::structValue(computed_quantile));
    }
  }

  if (found_used_histogram) {
    (*histograms_obj_fields)["computed_quantiles"] = ValueUtil::listValue(computed_quantile_array);
    (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj);
    stats_array.push_back(ValueUtil::structValue(histograms_obj_container));
  }

  auto* document_fields = document.mutable_fields();
  (*document_fields)["stats"] = ValueUtil::listValue(stats_array);

  return MessageUtil::getJsonStringFromMessage(document, pretty_print, true);
}

} // namespace Server
} // namespace Envoy
