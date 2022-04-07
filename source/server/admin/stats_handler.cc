#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"
#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/memory/stats.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/stats_request.h"

#include "absl/strings/numbers.h"

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

Admin::RequestPtr StatsHandler::makeRequest(absl::string_view path, AdminStream& /*admin_stream*/) {
  StatsParams params;
  Buffer::OwnedImpl response;
  Http::Code code = params.parse(path, response);
  if (code != Http::Code::OK) {
    return Admin::makeStaticTextRequest(response, code);
  }

  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  if (params.format_ == StatsFormat::Prometheus) {
    // TODO(#16139): modify streaming algorithm to cover Prometheus.
    //
    // This may be easiest to accomplish by populating the set
    // with tagExtractedName(), and allowing for vectors of
    // stats as multiples will have the same tag-extracted names.
    // Ideally we'd find a way to do this without slowing down
    // the non-Prometheus implementations.
    Stats::Store& store = server_.stats();
    std::vector<Stats::ParentHistogramSharedPtr> histograms = store.histograms();
    store.symbolTable().sortByStatNames<Stats::ParentHistogramSharedPtr>(
        histograms.begin(), histograms.end(),
        [](const Stats::ParentHistogramSharedPtr& a) -> Stats::StatName { return a->statName(); });
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
        params.prometheus_text_readouts_ ? store.textReadouts()
        : std::vector<Stats::TextReadoutSharedPtr>();
    PrometheusStatsFormatter::statsAsPrometheus(
        store.counters(), store.gauges(), histograms, text_readouts_vec, response,
        params.used_only_, params.filter_, server_.api().customStatNamespaces());
    return Admin::makeStaticTextRequest(response, code);
  }
  return makeRequest(server_.stats(), params, [this]() -> Admin::UrlHandler {
    return statsHandler();
  });
}

Admin::RequestPtr StatsHandler::makeRequest(Stats::Store& stats, const StatsParams& params,
                                            StatsRequest::UrlHandlerFn url_handler_fn) {
  return std::make_unique<StatsRequest>(stats, params, url_handler_fn);
}

Http::Code StatsHandler::handlerPrometheusStats(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  return prometheusStats(path_and_query, response);
}

Http::Code StatsHandler::prometheusStats(absl::string_view path_and_query,
                                         Buffer::Instance& response) {
  StatsParams params;
  Http::Code code = params.parse(path_and_query, response);
  if (code != Http::Code::OK) {
    return code;
  }

  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  Stats::Store& stats = server_.stats();
  const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
      params.prometheus_text_readouts_ ? stats.textReadouts()
                                       : std::vector<Stats::TextReadoutSharedPtr>();
  PrometheusStatsFormatter::statsAsPrometheus(stats.counters(), stats.gauges(), stats.histograms(),
                                              text_readouts_vec, response, params.used_only_,
                                              params.filter_, server_.api().customStatNamespaces());
  return Http::Code::OK;
}

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

Admin::UrlHandler StatsHandler::statsHandler() {
  return {"/stats", "print server stats",
    [this](absl::string_view path, AdminStream& admin_stream) -> Admin::RequestPtr {
      return makeRequest(path, admin_stream);
    },
    false, false,
    {{Admin::ParamDescriptor::Type::Boolean, "usedonly",
       "Only include stats that have been written by system since restart"},
     {Admin::ParamDescriptor::Type::String, "filter",
      "Regular expression (ecmascript) for filtering stats"},
     {Admin::ParamDescriptor::Type::Enum,
      "format",
      "File format to use.",
      {"html", "text", "json"}},
     {Admin::ParamDescriptor::Type::Enum,
      "type",
      "Stat types to include.",
      {Labels::All, Labels::Counters, Labels::Histograms, Labels::Gauges,
       Labels::TextReadouts}}}};
}

} // namespace Server
} // namespace Envoy

/*
Http::Code StatsHandler::handlerStats(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
*/

#if 0
class StatsHandler::Context {
public:
#define STRING_SORT 1
#if STRING_SORT
  template <class ValueType> using NameValue = std::pair<std::string, ValueType>;
  template <class ValueType> using NameValueVec = std::vector<NameValue<ValueType>>;
#else
  template <class StatType> using StatVec = std::vector<Stats::RefcountPtr<StatType>>;
#endif

  Context(const Params& params, Render& render, Buffer::Instance& response)
      : params_(params), render_(render), response_(response) {}

  // Iterates through the various stat types, and renders them.
  void collectAndEmitStats(const Stats::Store& stats) {
    uint64_t mem_start = Memory::Stats::totalCurrentlyAllocated();
#if STRING_SORT
    NameValueVec<std::string> text_readouts;
    collect<Stats::TextReadout>(Type::TextReadouts, stats, text_readouts);
    emit<std::string>(text_readouts);

    // We collect counters and gauges together and co-mingle them before sorting.
    // Note that the user can use type 'type=' query-param to show only desired
    // type; the default is All.
    NameValueVec<uint64_t> counters_and_gauges;
    collect<Stats::Counter>(Type::Counters, stats, counters_and_gauges);
    collect<Stats::Gauge>(Type::Gauges, stats, counters_and_gauges);
    ENVOY_LOG_MISC(error, "Memory Peak: {}", Memory::Stats::totalCurrentlyAllocated() - mem_start);
    emit<uint64_t>(counters_and_gauges);

    NameValueVec<Stats::ParentHistogramSharedPtr> histograms;
    collect<Stats::ParentHistogram>(Type::Histograms, stats, histograms);
    emit<Stats::ParentHistogramSharedPtr>(histograms);
#else
    const Stats::SymbolTable& symbol_table = stats.constSymbolTable();

    StatVec<Stats::TextReadout> text_readouts;
    collect<Stats::TextReadout>(Type::TextReadouts, stats, text_readouts);
    emit<Stats::TextReadout>(text_readouts, symbol_table);

    // TODO(jmarantz): merge-sort the counter & stats values together.
    StatVec<Stats::Counter> counters;
    StatVec<Stats::Gauge> gauges;
    collect<Stats::Counter>(Type::Counters, stats, counters);
    collect<Stats::Gauge>(Type::Gauges, stats, gauges);
    ENVOY_LOG_MISC(error, "Memory Peak: {}", Memory::Stats::totalCurrentlyAllocated() - mem_start);

    emit<Stats::Counter>(counters, symbol_table);
    emit<Stats::Gauge>(gauges, symbol_table);

    StatVec<Stats::ParentHistogram> histograms;
    collect<Stats::ParentHistogram>(Type::Histograms, stats, histograms);
    emit<Stats::ParentHistogram>(histograms, symbol_table);
#endif
  }

#if STRING_SORT
  template <class ValueType> void emit(NameValueVec<ValueType>& name_value_vec) {
    std::sort(name_value_vec.begin(), name_value_vec.end());
    for (NameValue<ValueType>& name_value : name_value_vec) {
      render_.generate(name_value.first, name_value.second);
      name_value = NameValue<ValueType>(); // free memory after rendering
    }
  }
#else
  template <class StatType>
  void emit(StatVec<StatType>& stat_vec, const Stats::SymbolTable& symbol_table) {
    struct Compare {
      Compare(const Stats::SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
      bool operator()(const Stats::RefcountPtr<StatType>& a,
                      const Stats::RefcountPtr<StatType>& b) {
        return symbol_table_.lessThan(a->statName(), b->statName());
      }
      const Stats::SymbolTable& symbol_table_;
    };

    std::sort(stat_vec.begin(), stat_vec.end(), Compare(symbol_table));
    for (Stats::RefcountPtr<StatType>& stat : stat_vec) {
      render_.generate(stat->name(), saveValue(*stat));
      stat.reset(); // free memory after rendering
    }
  }
#endif

#if STRING_SORT
  template <class StatType, class ValueType>
  void collect(Type type, const Stats::Store& stats, NameValueVec<ValueType>& vec)
#else
  template <class StatType>
  void collect(Type type, const Stats::Store& stats, StatVec<StatType>& vec)
#endif
  {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    size_t previous_size = vec.size();
    collectHelper(stats, [this, &vec](StatType& stat) {
      if (params_.shouldShowMetric(stat)) {
#if STRING_SORT
        vec.emplace_back(std::make_pair(stat.name(), saveValue(stat)));
#else
        vec.emplace_back(Stats::RefcountPtr<StatType>(&stat));
#endif
      }
    });
    if (previous_size == vec.size()) {
      render_.noStats(type);
    }
  }

  std::string saveValue(Stats::TextReadout& text_readout) { return text_readout.value(); }
  uint64_t saveValue(Stats::Counter& counter) { return counter.value(); }
  uint64_t saveValue(Stats::Gauge& gauge) { return gauge.value(); }
  Stats::ParentHistogramSharedPtr saveValue(Stats::ParentHistogram& histogram) {
    return Stats::ParentHistogramSharedPtr(&histogram);
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::TextReadout&> fn) {
    stats.forEachTextReadout(nullptr, fn);
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::Counter&> fn) {
    stats.forEachCounter(nullptr, fn);
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::Gauge&> fn) {
    stats.forEachGauge(nullptr, fn);
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::ParentHistogram> fn) {
    // TODO(jmarantz)): when #19166 lands convert to stats.forEachHistogram(nullptr, fn);
    for (Stats::ParentHistogramSharedPtr& histogram : stats.histograms()) {
      fn(*histogram);
    }
  }

  const Params& params_;
  Render& render_;
  Buffer::Instance& response_;
};

 bool used_only, bool json,
                           Utility::HistogramBucketsMode histogram_buckets_mode,
                           absl::optional<std::regex> regex
 bool used_only, bool json,
                           Utility::HistogramBucketsMode histogram_buckets_mode,
                           absl::optional<std::regex> regexy
       std::unique_ptr<Render> render;

  switch (params.format_) {
  case Format::Html:
    render = std::make_unique<HtmlRender>(response_headers, response, *this, params);
    break;
  case Format::Json:
    render = std::make_unique<JsonRender>(response_headers, response, params);
    break;
  case Format::Prometheus:
    // TODO(#16139): modify streaming algorithm to cover Prometheus.
    //
    // This may be easiest to accomplish by populating the set
    // with tagExtractedName(), and allowing for vectors of
    // stats as multiples will have the same tag-extracted names.
    // Ideally we'd find a way to do this without slowing down
    // the non-Prometheus implementations.
    return Admin::makeStaticTextRequest(response, prometheusStats(path, response));
  case Format::Text:
    render = std::make_unique<TextRender>(response);
    break;
  }

  Context context(params, *render, response);
  context.collectAndEmitStats(stats);
  render->render();
  return Http::Code::OK;

  /*
  const Http::Utility::QueryParams params = Http::Utility::parseAndDecodeQueryString(path);

  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  Buffer::OwnedImpl response;
  if (!Utility::filterParam(params, response, regex)) {
    return Admin::makeStaticTextRequest(response, Http::Code::BadRequest);
  }

  // If the histogram_buckets query param does not exist histogram output should contain quantile
  // summary data. Using histogram_buckets will change output to show bucket data. The
  // histogram_buckets query param has two possible values: cumulative or disjoint.
  Utility::HistogramBucketsMode histogram_buckets_mode = Utility::HistogramBucketsMode::NoBuckets;
  absl::Status histogram_buckets_status =
      Utility::histogramBucketsParam(params, histogram_buckets_mode);
  if (!histogram_buckets_status.ok()) {
    return Admin::makeStaticTextRequest(histogram_buckets_status.message(), Http::Code::BadRequest);
  }

  const absl::optional<std::string> format_value = Utility::formatParam(params);
  bool json = false;
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
    } else if (format_value.value() == "json") {
      json = true;
    } else {
      return Admin::makeStaticTextRequest(
          "usage: /stats?format=json  or /stats?format=prometheus \n\n", Http::Code::BadRequest);
    }
  }

  return makeRequest(server_.stats(), used_only, json, histogram_buckets_mode, regex);
  */

#endif
