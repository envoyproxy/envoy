#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/prometheus_stats.h"

namespace Envoy {
namespace Server {

const uint64_t RecentLookupsCapacity = 100;

StatsHandler::StatsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code StatsHandler::handlerResetCounters(absl::string_view, Http::ResponseHeaderMap&,
                                              Buffer::Instance& response, AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookups(absl::string_view, Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response, AdminStream&) {
  Stats::SymbolTable& symbol_table = server_.stats().symbolTable();
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
                                                        Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsDisable(absl::string_view,
                                                          Http::ResponseHeaderMap&,
                                                          Buffer::Instance& response,
                                                          AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsEnable(absl::string_view,
                                                         Http::ResponseHeaderMap&,
                                                         Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStats(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream& admin_stream) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  const Http::Utility::QueryParams params = Http::Utility::parseAndDecodeQueryString(url);

  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  if (!Utility::filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }

  // If the histogram_buckets query param does not exist histogram output should contain quantile
  // summary data. Using histogram_buckets will change output to show bucket data. The
  // histogram_buckets query param has two possible values: cumulative or disjoint.
  Utility::HistogramBucketsValue histogram_buckets_value = Utility::HistogramBucketsValue::Null;
  if (!Utility::histogramBucketsParam(params, response, histogram_buckets_value)) {
    return Http::Code::BadRequest;
  }

  const absl::optional<std::string> format_value = Utility::formatParam(params);
  if (format_value.has_value() && format_value.value() == "prometheus") {
    return handlerPrometheusStats(url, response_headers, response, admin_stream);
  }

  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    if (shouldShowMetric(*counter, used_only, regex)) {
      all_stats.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : server_.stats().gauges()) {
    if (shouldShowMetric(*gauge, used_only, regex)) {
      ASSERT(gauge->importMode() != Stats::Gauge::ImportMode::Uninitialized);
      all_stats.emplace(gauge->name(), gauge->value());
    }
  }

  std::map<std::string, std::string> text_readouts;
  for (const auto& text_readout : server_.stats().textReadouts()) {
    if (shouldShowMetric(*text_readout, used_only, regex)) {
      text_readouts.emplace(text_readout->name(), text_readout->value());
    }
  }

  if (!format_value.has_value()) {
    // Display plain stats if format query param is not there.
    statsAsText(all_stats, text_readouts, server_.stats().histograms(), used_only,
                histogram_buckets_value, regex, response);
    return Http::Code::OK;
  }

  if (format_value.value() == "json") {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    response.add(statsAsJson(all_stats, text_readouts, server_.stats().histograms(), used_only,
                             histogram_buckets_value, regex));
    return Http::Code::OK;
  }

  response.add("usage: /stats?format=json  or /stats?format=prometheus \n");
  response.add("\n");
  return Http::Code::NotFound;
}

Http::Code StatsHandler::handlerPrometheusStats(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams params =
      Http::Utility::parseAndDecodeQueryString(path_and_query);
  const bool used_only = params.find("usedonly") != params.end();
  const bool text_readouts = params.find("text_readouts") != params.end();

  const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
      text_readouts ? server_.stats().textReadouts() : std::vector<Stats::TextReadoutSharedPtr>();

  absl::optional<std::regex> regex;
  if (!Utility::filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }

  PrometheusStatsFormatter::statsAsPrometheus(
      server_.stats().counters(), server_.stats().gauges(), server_.stats().histograms(),
      text_readouts_vec, response, used_only, regex, server_.api().customStatNamespaces());
  return Http::Code::OK;
}

// TODO(ambuc) Export this as a server (?) stat for monitoring.
Http::Code StatsHandler::handlerContention(absl::string_view,
                                           Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response, AdminStream&) {

  if (server_.options().mutexTracingEnabled() && server_.mutexTracer() != nullptr) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

    envoy::admin::v3::MutexStats mutex_stats;
    mutex_stats.set_num_contentions(server_.mutexTracer()->numContentions());
    mutex_stats.set_current_wait_cycles(server_.mutexTracer()->currentWaitCycles());
    mutex_stats.set_lifetime_wait_cycles(server_.mutexTracer()->lifetimeWaitCycles());
    response.add(MessageUtil::getJsonStringFromMessageOrError(mutex_stats, true, true));
  } else {
    response.add("Mutex contention tracing is not enabled. To enable, run Envoy with flag "
                 "--enable-mutex-tracing.");
  }
  return Http::Code::OK;
}

void StatsHandler::statsAsText(const std::map<std::string, uint64_t>& all_stats,
                               const std::map<std::string, std::string>& text_readouts,
                               const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                               bool used_only,
                               Utility::HistogramBucketsValue histogram_buckets_value,
                               const absl::optional<std::regex>& regex,
                               Buffer::Instance& response) {
  // Display plain stats if format query param is not there.
  for (const auto& text_readout : text_readouts) {
    response.addFragments(
        {text_readout.first, ": \"", Html::Utility::sanitize(text_readout.second), "\"\n"});
  }
  for (const auto& stat : all_stats) {
    response.addFragments({stat.first, ": ", absl::StrCat(stat.second), "\n"});
  }
  std::map<std::string, std::string> all_histograms;
  for (const Stats::ParentHistogramSharedPtr& histogram : histograms) {
    if (shouldShowMetric(*histogram, used_only, regex)) {
      bool emplace_success = false;
      // Display bucket data if histogram_buckets query parameter is used, otherwise output contains
      // quantile summary data.
      switch (histogram_buckets_value) {
      case Utility::HistogramBucketsValue::Null:
        emplace_success =
            all_histograms.emplace(histogram->name(), histogram->quantileSummary()).second;
        break;
      case Utility::HistogramBucketsValue::Cumulative:
        emplace_success =
            all_histograms.emplace(histogram->name(), histogram->bucketSummary()).second;
        break;
      case Utility::HistogramBucketsValue::Disjoint:
        emplace_success =
            all_histograms.emplace(histogram->name(), computeDisjointBucketSummary(histogram))
                .second;
        break;
      default:
        break;
      }
      ASSERT(emplace_success); // No duplicates expected.
    }
  }
  for (const auto& histogram : all_histograms) {
    response.addFragments({histogram.first, ": ", histogram.second, "\n"});
  }
}

std::string
StatsHandler::statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                          const std::map<std::string, std::string>& text_readouts,
                          const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                          const bool used_only,
                          Utility::HistogramBucketsValue histogram_buckets_value,
                          const absl::optional<std::regex>& regex, const bool pretty_print) {

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

  ProtobufWkt::Struct histograms_obj_container;
  auto* histograms_obj_container_fields = histograms_obj_container.mutable_fields();

  // Display bucket data if histogram_buckets query parameter is used, otherwise output contains
  // quantile summary data.
  switch (histogram_buckets_value) {
  case Utility::HistogramBucketsValue::Null:
    statsAsJsonQuantileSummaryHelper(*histograms_obj_container_fields, all_histograms, used_only,
                                     regex);
    break;
  case Utility::HistogramBucketsValue::Cumulative:
    statsAsJsonHistogramBucketsHelper(
        *histograms_obj_container_fields, all_histograms, used_only, regex,
        [](const Stats::ParentHistogramSharedPtr& histogram) {
          return histogram->intervalStatistics().computedBuckets();
        },
        [](const Stats::ParentHistogramSharedPtr& histogram) {
          return histogram->cumulativeStatistics().computedBuckets();
        });
    break;
  case Utility::HistogramBucketsValue::Disjoint:
    statsAsJsonHistogramBucketsHelper(
        *histograms_obj_container_fields, all_histograms, used_only, regex,
        [](const Stats::ParentHistogramSharedPtr& histogram) {
          return histogram->intervalStatistics().computeDisjointBuckets();
        },
        [](const Stats::ParentHistogramSharedPtr& histogram) {
          return histogram->cumulativeStatistics().computeDisjointBuckets();
        });
    break;
  default:
    break;
  }

  // Add histograms to output if used histogram found.
  if (histograms_obj_container_fields->contains("histograms")) {
    stats_array.push_back(ValueUtil::structValue(histograms_obj_container));
  }

  auto* document_fields = document.mutable_fields();
  (*document_fields)["stats"] = ValueUtil::listValue(stats_array);

  return MessageUtil::getJsonStringFromMessageOrDie(document, pretty_print, true);
}

std::string
StatsHandler::computeDisjointBucketSummary(const Stats::ParentHistogramSharedPtr& histogram) {
  if (!histogram->used()) {
    return "No recorded values";
  }
  std::vector<std::string> bucket_summary;
  Stats::ConstSupportedBuckets& supported_buckets =
      histogram->intervalStatistics().supportedBuckets();
  const std::vector<uint64_t> disjoint_interval_buckets =
      histogram->intervalStatistics().computeDisjointBuckets();
  const std::vector<uint64_t> disjoint_cumulative_buckets =
      histogram->cumulativeStatistics().computeDisjointBuckets();
  bucket_summary.reserve(supported_buckets.size());
  // Make sure all vectors are the same size.
  ASSERT(disjoint_interval_buckets.size() == disjoint_cumulative_buckets.size());
  ASSERT(disjoint_cumulative_buckets.size() == supported_buckets.size());
  size_t min_size = std::min({disjoint_interval_buckets.size(), disjoint_cumulative_buckets.size(),
                              supported_buckets.size()});
  for (size_t i = 0; i < min_size; ++i) {
    bucket_summary.push_back(fmt::format("B{:g}({},{})", supported_buckets[i],
                                         disjoint_interval_buckets[i],
                                         disjoint_cumulative_buckets[i]));
  }
  return absl::StrJoin(bucket_summary, " ");
}

void StatsHandler::statsAsJsonQuantileSummaryHelper(
    Protobuf::Map<std::string, ProtobufWkt::Value>& histograms_obj_container_fields,
    const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms, bool used_only,
    const absl::optional<std::regex>& regex) {
  std::vector<ProtobufWkt::Value> computed_quantile_array;
  for (const Stats::ParentHistogramSharedPtr& histogram : all_histograms) {
    if (shouldShowMetric(*histogram, used_only, regex)) {
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

  if (!computed_quantile_array.empty()) { // If used histogram found.
    ProtobufWkt::Struct histograms_obj;
    auto* histograms_obj_fields = histograms_obj.mutable_fields();

    // It is not possible for the supported quantiles to differ across histograms, so it is ok
    // to send them once.
    Stats::HistogramStatisticsImpl empty_statistics;
    std::vector<ProtobufWkt::Value> supported_quantile_array;
    for (double quantile : empty_statistics.supportedQuantiles()) {
      supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
    }
    (*histograms_obj_fields)["supported_quantiles"] =
        ValueUtil::listValue(supported_quantile_array);

    (*histograms_obj_fields)["computed_quantiles"] = ValueUtil::listValue(computed_quantile_array);

    histograms_obj_container_fields["histograms"] = ValueUtil::structValue(histograms_obj);
  }
}

void StatsHandler::statsAsJsonHistogramBucketsHelper(
    Protobuf::Map<std::string, ProtobufWkt::Value>& histograms_obj_container_fields,
    const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms, bool used_only,
    const absl::optional<std::regex>& regex,
    std::function<std::vector<uint64_t>(const Stats::ParentHistogramSharedPtr&)>
        interval_computed_buckets,
    std::function<std::vector<uint64_t>(const Stats::ParentHistogramSharedPtr&)>
        cumulative_computed_buckets) {
  std::vector<ProtobufWkt::Value> histogram_obj_array;
  for (const Stats::ParentHistogramSharedPtr& histogram : all_histograms) {
    if (shouldShowMetric(*histogram, used_only, regex)) {
      histogram_obj_array.push_back(statsAsJsonHistogramBucketsCreateHistogramElementHelper(
          histogram->intervalStatistics().supportedBuckets(), interval_computed_buckets(histogram),
          cumulative_computed_buckets(histogram), histogram->name()));
    }
  }
  if (!histogram_obj_array.empty()) { // If used histogram found.
    histograms_obj_container_fields["histograms"] = ValueUtil::listValue(histogram_obj_array);
  }
}

ProtobufWkt::Value StatsHandler::statsAsJsonHistogramBucketsCreateHistogramElementHelper(
    Stats::ConstSupportedBuckets& supported_buckets, const std::vector<uint64_t>& interval_buckets,
    const std::vector<uint64_t>& cumulative_buckets, const std::string& name) {
  ProtobufWkt::Struct histogram_obj;
  auto* histogram_obj_fields = histogram_obj.mutable_fields();
  (*histogram_obj_fields)["name"] = ValueUtil::stringValue(name);

  // Make sure all vectors are the same size.
  ASSERT(interval_buckets.size() == cumulative_buckets.size());
  ASSERT(cumulative_buckets.size() == supported_buckets.size());
  size_t min_size =
      std::min({interval_buckets.size(), cumulative_buckets.size(), supported_buckets.size()});

  std::vector<ProtobufWkt::Value> bucket_array;
  for (size_t i = 0; i < min_size; ++i) {
    ProtobufWkt::Struct bucket;
    auto* bucket_fields = bucket.mutable_fields();
    (*bucket_fields)["upper_bound"] = ValueUtil::numberValue(supported_buckets[i]);
    (*bucket_fields)["interval"] = ValueUtil::numberValue(interval_buckets[i]);
    (*bucket_fields)["cumulative"] = ValueUtil::numberValue(cumulative_buckets[i]);
    bucket_array.push_back(ValueUtil::structValue(bucket));
  }
  (*histogram_obj_fields)["buckets"] = ValueUtil::listValue(bucket_array);

  return ValueUtil::structValue(histogram_obj);
}

} // namespace Server
} // namespace Envoy
