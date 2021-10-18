#include "source/server/admin/stats_handler.h"

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/stats/filter.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/utils.h"

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

  const absl::optional<std::string> format_value = Utility::formatParam(params);
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      return handlerPrometheusStats(url, response_headers, response, admin_stream);
    }
    if (format_value.value() == "html") {
      return statsAsHtml(params, response_headers, response, admin_stream, used_only, regex);
    }
  }

  if (params.find("scopes") != params.end()) {
    Stats::StatNameHashSet prefixes;
    server_.stats().forEachScope([](size_t) {}, [&prefixes](const Stats::Scope& scope) {
      prefixes.insert(scope.prefix());
    });
    std::vector<std::string> names;
    names.reserve(prefixes.size());
    for (Stats::StatName prefix : prefixes) {
      names.emplace_back(server_.stats().symbolTable().toString(prefix));
    }
    std::sort(names.begin(), names.end());
    for (std::string& name : names) {
      name = absl::StrCat("<a href='/stats?scope=", name, "'>", name, "</a>");
    }
    response.add(absl::StrJoin(names, "<br>\n"));
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
    return Http::Code::OK;
  }

  std::map<std::string, uint64_t> all_stats;
  std::map<std::string, std::string> text_readouts;
  std::vector<Stats::HistogramSharedPtr> histograms;
  auto append_stats_from_scope = [&all_stats, used_only, &regex, &text_readouts, &histograms]
                                 (const Stats::Scope& scope) {
    Stats::IterateFn<Stats::Counter> cfn =
        [&all_stats, used_only, &regex](const Stats::CounterSharedPtr& counter) -> bool {
          if (shouldShowMetric(*counter, used_only, regex)) {
            all_stats.emplace(counter->name(), counter->value());
          }
          return true;
        };
    scope.iterate(cfn);

    Stats::IterateFn<Stats::Gauge> gfn =
        [&all_stats, used_only, &regex](const Stats::GaugeSharedPtr& gauge) -> bool {
          if (shouldShowMetric(*gauge, used_only, regex)) {
            ASSERT(gauge->importMode() != Stats::Gauge::ImportMode::Uninitialized);
            all_stats.emplace(gauge->name(), gauge->value());
          }
          return true;
        };
    scope.iterate(gfn);

    Stats::IterateFn<Stats::TextReadout> tfn =
        [&text_readouts, used_only, &regex](const Stats::TextReadoutSharedPtr& text_readout) -> bool {
          if (shouldShowMetric(*text_readout, used_only, regex)) {
            text_readouts.emplace(text_readout->name(), text_readout->value());
          }
          return true;
        };
    scope.iterate(tfn);

    Stats::IterateFn<Stats::Histogram> hfn =
        [&histograms, used_only, &regex](const Stats::HistogramSharedPtr& histogram) -> bool {
          if (shouldShowMetric(*histogram, used_only, regex)) {
            histograms.push_back(histogram);
          }
          return true;
        };
    scope.iterate(hfn);
  };

  Http::Utility::QueryParams::const_iterator scope_iter = params.find("scope");
  if (scope_iter == params.end()) {
    append_stats_from_scope(server_.stats());
  } else {
    Stats::StatNameManagedStorage scope_name(scope_iter->second, server_.stats().symbolTable());
    server_.stats().forEachScope([](size_t) {}, [&scope_name, &append_stats_from_scope](
        const Stats::Scope& scope) {
      if (scope.prefix() == scope_name.statName()) {
        append_stats_from_scope(scope);
      }
    });
  }

  if (!format_value.has_value()) {
    // Display plain stats if format query param is not there.
    statsAsText(all_stats, text_readouts, histograms, response);
    return Http::Code::OK;
  }

  if (format_value.value() == "json") {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    response.add(statsAsJson(all_stats, text_readouts, histograms));
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
  absl::optional<std::regex> regex;
  if (!Utility::filterParam(params, response, regex)) {
    return Http::Code::BadRequest;
  }
  PrometheusStatsFormatter::statsAsPrometheus(server_.stats().counters(), server_.stats().gauges(),
                                              server_.stats().histograms(), response, used_only,
                                              regex, server_.api().customStatNamespaces());
  return Http::Code::OK;
}

Http::Code StatsHandler::statsAsHtml(const Http::Utility::QueryParams& params,
                                     Http::ResponseHeaderMap& response_headers,
                                     Buffer::Instance& response, AdminStream&, bool used_only,
                                     absl::optional<std::regex>& filter) {

  Stats::StatNamePool pool(server_.stats().symbolTable());
  Stats::StatName after;
  Http::Utility::QueryParams::const_iterator after_iter = params.find("after");
  if (after_iter != params.end()) {
    after = pool.add(after_iter->second);
  }

  uint32_t page_size = 1000;
  Http::Utility::QueryParams::const_iterator page_size_iter = params.find("psize");
  if (page_size_iter != params.end()) {
    if (!absl::SimpleAtoi(page_size_iter->second, &page_size)) {
      response.add("Invalid page size found after &psize=");
      return Http::Code::BadRequest;
    }
  }

  Http::Utility::QueryParams::const_iterator type_iter = params.find("type");
  absl::string_view type = "counters";
  if (type_iter != params.end()) {
    type = type_iter->second;
  }

  if (type == "counters") {
    renderHtml<Stats::Counter>(after, page_size, used_only, filter, response_headers, response);
  } else if (type == "gauges") {
    renderHtml<Stats::Gauge>(after, page_size, used_only, filter, response_headers, response);
  } else if (type == "text-readouts") {
    renderHtml<Stats::TextReadout>(after, page_size, used_only, filter, response_headers, response);
  } else if (type == "histograms") {
    //renderHtml<Stats::Histogram>(after, page_size, used_only, filter, response_headers, response);
  } else {
    response.add("Invalid page &type= value");
    return Http::Code::BadRequest;
  }

  return Http::Code::OK;
}

template<class StatType> Http::Code StatsHandler::renderHtml(
    Stats::StatName after, uint32_t page_size, bool used_only,
    absl::optional<std::regex>& /*filter*/,
    Http::ResponseHeaderMap& /*response_headers*/,
    Buffer::Instance& response) {

  Stats::StatsFilter<StatType> filter(server_.stats());
  filter.setUsedOnly(used_only);
  std::vector<Stats::RefcountPtr<StatType>> stats;
  stats = filter.getFilteredStatsAfter(page_size, after);
  std::vector<std::string> out;
  out.reserve(2 + stats.size());
  out.push_back("<html><pre>\n");
  for (const auto& stat : stats) {
    out.push_back(absl::StrCat(stat->name(), ": ", stat->value(), "\n"));
  }
  out.push_back("</pre></html>\n");
  response.add(absl::StrJoin(out, ""));
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
                               const std::vector<Stats::HistogramSharedPtr>& histograms,
                               Buffer::Instance& response) {
  // Display plain stats if format query param is not there.
  for (const auto& text_readout : text_readouts) {
    response.add(fmt::format("{}: \"{}\"\n", text_readout.first,
                             Html::Utility::sanitize(text_readout.second)));
  }
  for (const auto& stat : all_stats) {
    response.add(fmt::format("{}: {}\n", stat.first, stat.second));
  }
  for (const auto& histogram : histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
      response.add(fmt::format("{}: {}\n", phist->name(), phist->quantileSummary()));
    }
  }
}

std::string
StatsHandler::statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                          const std::map<std::string, std::string>& text_readouts,
                          const std::vector<Stats::HistogramSharedPtr>& all_histograms,
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
  for (const Stats::HistogramSharedPtr& histogram : all_histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
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
      for (size_t i = 0; i < phist->intervalStatistics().supportedQuantiles().size(); ++i) {
        ProtobufWkt::Struct computed_quantile_value;
        auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
        const auto& interval = phist->intervalStatistics().computedQuantiles()[i];
        const auto& cumulative = phist->cumulativeStatistics().computedQuantiles()[i];
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

  return MessageUtil::getJsonStringFromMessageOrDie(document, pretty_print, true);
}

} // namespace Server
} // namespace Envoy
