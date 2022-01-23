#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/memory/stats.h"
#include "source/server/admin/admin_html_generator.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/utils.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Server {

namespace {

constexpr absl::string_view AllLabel = "All";
constexpr absl::string_view CountersLabel = "Counters";
constexpr absl::string_view GaugesLabel = "Gauges";
constexpr absl::string_view HistogramsLabel = "Histograms";
constexpr absl::string_view TextReadoutsLabel = "TextReadouts";

} // namespace

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

bool StatsHandler::Params::shouldShowMetric(const Stats::Metric& metric) const {
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
}

Http::Code StatsHandler::Params::parse(absl::string_view url, Buffer::Instance& response) {
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

  auto parse_type = [](absl::string_view str, Type& type) {
    if (str == GaugesLabel) {
      type = Type::Gauges;
    } else if (str == CountersLabel) {
      type = Type::Counters;
    } else if (str == HistogramsLabel) {
      type = Type::Histograms;
    } else if (str == TextReadoutsLabel) {
      type = Type::TextReadouts;
    } else if (str == AllLabel) {
      type = Type::All;
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
      format_ = Format::Prometheus;
    } else if (format_value.value() == "json") {
      format_ = Format::Json;
    } else if (format_value.value() == "text") {
      format_ = Format::Text;
    } else if (format_value.value() == "html") {
      format_ = Format::Html;
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n\n");
      return Http::Code::BadRequest;
    }
  }

  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStats(absl::string_view url,
                                      Http::ResponseHeaderMap& response_headers,
                                      Buffer::Instance& response,
                                      AdminStream& admin_stream) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  Stats::Store& store = server_.stats();
  if (params.format_ == Format::Prometheus) {
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
        params.prometheus_text_readouts_ ? store.textReadouts()
                                         : std::vector<Stats::TextReadoutSharedPtr>();
    PrometheusStatsFormatter::statsAsPrometheus(
        store.counters(), store.gauges(), store.histograms(), text_readouts_vec, response,
        params.used_only_, params.filter_, server_.api().customStatNamespaces());
    return Http::Code::OK;
  }

  auto iter = context_map_.find(&admin_stream);
  if (iter == context_map_.end()) {
    auto insertion = context_map_.emplace(&admin_stream, std::make_unique<Context>(
        params, server_.stats(), response_headers, response));
    iter = insertion.first;
  }
  Context& context = *iter->second;
  Http::Code code = context.nextChunk(response);
  if (code ! Http::Code::Continue) {
    context_map_.erase(iter);
  }
  return code;
}

class StatsHandler::Render {
public:
  virtual ~Render() = default;
  virtual void generate(const std::string& name, uint64_t value) PURE;
  virtual void generate(const std::string& name, const std::string& value) PURE;
  virtual void generate(const std::string& name,
                        const Stats::ParentHistogramSharedPtr& histogram) PURE;
  virtual void noStats(Type type) { UNREFERENCED_PARAMETER(type); }
  virtual void render() PURE;
};

class StatsHandler::TextRender : public StatsHandler::Render {
public:
  TextRender(Buffer::Instance& response) : response_(response) {}

  void generate(const std::string& name, uint64_t value) override {
    response_.addFragments({name, ": ", absl::StrCat(value), "\n"});
  }

  void generate(const std::string& name, const std::string& value) override {
    response_.addFragments({name, ": \"", Html::Utility::sanitize(value), "\"\n"});
  }

  void generate(const std::string& name,
                const Stats::ParentHistogramSharedPtr& histogram) override {
    response_.addFragments({name, ": ", histogram->quantileSummary(), "\n"});
  }

  void render() override {}

protected:
  Buffer::Instance& response_;
};

class StatsHandler::HtmlRender : public StatsHandler::TextRender {
public:
  HtmlRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
             StatsHandler& stats_handler, const Params& params)
      : TextRender(response), html_(response) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
    html_.setSubmitOnChange(true);
    html_.renderHead();
    response_.add("<body>\n");
    html_.renderTableBegin();
    html_.renderUrlHandler(stats_handler.statsHandler(), params.query_);
    html_.renderInput("scope", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    html_.renderTableEnd();
    response_.add("<pre>\n");
  }

  ~HtmlRender() override { response_.add("</pre></body>\n"); }

  void noStats(Type type) override {
    response_.add(
        absl::StrCat("</pre>\n<br/><i>No ", typeToString(type), " found</i><br/>\n<pre>\n"));
  }

  void render() override {}

private:
  AdminHtmlGenerator html_;
};

class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  JsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
             const Params& params)
      : response_(response), params_(params) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  }

  void generate(const std::string& name, uint64_t value) override {
    add(name, ValueUtil::numberValue(value));
  }
  void generate(const std::string& name, const std::string& value) override {
    add(name, ValueUtil::stringValue(value));
  }
  void generate(const std::string& name,
                const Stats::ParentHistogramSharedPtr& histogram) override {
    if (!found_used_histogram_) {
      auto* histograms_obj_fields = histograms_obj_.mutable_fields();

      // It is not possible for the supported quantiles to differ across histograms, so it is ok
      // to send them once.
      Stats::HistogramStatisticsImpl empty_statistics;
      std::vector<ProtobufWkt::Value> supported_quantile_array;
      for (double quantile : empty_statistics.supportedQuantiles()) {
        supported_quantile_array.push_back(ValueUtil::numberValue(quantile * 100));
      }
      (*histograms_obj_fields)["supported_quantiles"] =
          ValueUtil::listValue(supported_quantile_array);
      found_used_histogram_ = true;
    }

    ProtobufWkt::Struct computed_quantile;
    auto* computed_quantile_fields = computed_quantile.mutable_fields();
    (*computed_quantile_fields)["name"] = ValueUtil::stringValue(name);

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
    computed_quantile_array_.push_back(ValueUtil::structValue(computed_quantile));
  }

  void render() override {
    if (found_used_histogram_) {
      auto* histograms_obj_fields = histograms_obj_.mutable_fields();
      (*histograms_obj_fields)["computed_quantiles"] =
          ValueUtil::listValue(computed_quantile_array_);
      auto* histograms_obj_container_fields = histograms_obj_container_.mutable_fields();
      (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj_);
      stats_array_.push_back(ValueUtil::structValue(histograms_obj_container_));
    }

    auto* document_fields = document_.mutable_fields();
    (*document_fields)["stats"] = ValueUtil::listValue(stats_array_);
    response_.add(MessageUtil::getJsonStringFromMessageOrDie(document_, params_.pretty_, true));
  }

private:
  template <class Value> void add(const std::string& name, const Value& value) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(name);
    (*stat_obj_fields)["value"] = value;
    stats_array_.push_back(ValueUtil::structValue(stat_obj));
  }

  Buffer::Instance& response_;
  const StatsHandler::Params& params_;
  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  std::vector<ProtobufWkt::Value> scope_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
};

StatsHandler::Context::Context(
    const Params& params, Stats::Store& store, Http::ResponseHeaderMap& response_headers,
    Buffer::Instance& response,
    mem_start_(Memory::Stats::totalCurrentlyAllocated())
    : params_(params),
      symbol_table_(store_.symbolTable()) {
  switch (params.format_) {
  case Format::Html:
    render_ = std::make_unique<HtmlRender>(response_headers, response, *this, params);
    break;
  case Format::Json:
    render_ = std::make_unique<JsonRender>(response_headers, response, params);
    break;
  case Format::Prometheus:
    ASSERT(false);
    ABSL_FALLTHROUGH_INTENDED;
  case Format::Text:
    render_ = std::make_unique<TextRender>(response);
    break;
  }
}

// Iterates through the various stat types, and renders them.
Http::Code StatsHandler::Context::nextChunk(const Stats::Store& stats) {
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

  if (!emit<Stats::TextReadout>(Type::TextReadouts, text_readouts_, text_readout_index_) ||
      !emit<Stats::Counter>(Type::Counters, counters_, counter_index_) ||
      !emit<Stats::Gauge>(Type::Gauges, gauges_, gauge_index_) ||
      !emit<Stats::ParentHistogram>(Type::Histograms, histograms_, histogram_index_)) {
    return Http::Code::Continue;
  }

  // TODO(jmarantz): merge-sort the counter & stats values together.
  phase_ = Type::Counters;
  collect<Stats::Counter>(Type::Counters, stats, counters);
  if (emit<Stats::TextReadout>(Type::Counters, text_readouts_, text_readout_index_)) {
      phase_ = Type::Counters;
    }


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
template <class ValueType> void StatsHandler::Context::emit(NameValueVec<ValueType>& name_value_vec) {
  std::sort(name_value_vec.begin(), name_value_vec.end());
  for (NameValue<ValueType>& name_value : name_value_vec) {
    render_.generate(name_value.first, name_value.second);
    name_value = NameValue<ValueType>(); // free memory after rendering
  }
}
#else
template <class StatType> bool StatsHandler::Context::emit(Type type,
                                                           StatVec<StatType>& stat_vec, uint32_t& index) {
  if (phase_ != type) {
    return true;
  }

  if (stat_vec.empty()) {
    collect<StatType>(type, stat_vec);
    if (stat_vec.empty()) {
      render_.noStats(type);
      nextPhase();
      return true;
    }

    struct Getter {
      Stats::StatName operator()(const Stats::RefcountPtr<StatType>& a) const {
        return a->statName();
      }
    };
    Stats::sortByStatNames<Stats::RefcountPtr<StatType>>(
        symbol_table, stat_vec.begin(), stat_vec.end(), Getter());
  }
  for (; index < stat_vec.size(); ++index) {
    Stats::RefcountPtr<StatType>& stat = stat_vec[index];
    render_.generate(stat->name(), saveValue(*stat));
    stat.reset(); // free memory after rendering
    if ((index % 20) == 0) {
      return false;
    }
  }
  stat_vec.clear();
  nextPhase();
  return true;
}
#endif

#if STRING_SORT
template <class StatType, class ValueType> void StatsHandler::Context::collect(
    Type type, NameValueVec<ValueType>& vec)
#else
template <class StatType> void StatsHandler::Context::collect(Type type, StatVec<StatType>& vec)
#endif
{
  // Bail early if the  requested type does not match the current type.
  if (params_.type_ != Type::All && params_.type_ != type) {
    return;
  }

  size_t previous_size = vec.size();

  collectHelper(stats,
                [this, &vec](size_t size) {
                  if (!params_.canRejectStats()) {
                    vec.reserve(vec.size() + size);
                  }
                },
                [this, &vec](StatType& stat) {
                  if (params_.shouldShowMetric(stat)) {
#if STRING_SORT
                    vec.emplace_back(std::make_pair(stat.name(), saveValue(stat)));
#else
                    vec.emplace_back(Stats::RefcountPtr<StatType>(&stat));
#endif
                  }
                });
  mem_max_ = std::max(mem_max_, Memory::Stats::totalCurrentlyAllocated());
  if (previous_size == vec.size()) {
    render_.noStats(type);
  }
}

void StatsHandler::Context::collectHelper(const Stats::Store& stats, std::function<void(size_t)> size_fn,
                   Stats::StatFn<Stats::ParentHistogram> fn) {
  // TODO(jmarantz)): when #19166 lands convert to stats.forEachHistogram(nullptr, fn);
  std::vector<Stats::ParentHistogramSharedPtr> histograms = stats.histograms();
  size_fn(histograms.size());
  for (Stats::ParentHistogramSharedPtr& histogram : histograms) {
    fn(*histogram);
  }
}

Http::Code StatsHandler::handlerPrometheusStats(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  Params params;
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

Admin::UrlHandler StatsHandler::statsHandler() {
  return {"/stats",
          "Print server stats.",
          MAKE_ADMIN_HANDLER(handlerStats),
          false,
          false,
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
            {AllLabel, CountersLabel, HistogramsLabel, GaugesLabel, TextReadoutsLabel}}}};
}

absl::string_view StatsHandler::typeToString(StatsHandler::Type type) {
  static constexpr absl::string_view TextReadouts = "TextReadouts";
  static constexpr absl::string_view Counters = "Counters";
  static constexpr absl::string_view Gauges = "Gauges";
  static constexpr absl::string_view Histograms = "Histograms";
  static constexpr absl::string_view All = "All";

  absl::string_view ret;
  switch (type) {
  case Type::TextReadouts:
    ret = TextReadouts;
    break;
  case Type::Counters:
    ret = Counters;
    break;
  case Type::Gauges:
    ret = Gauges;
    break;
  case Type::Histograms:
    ret = Histograms;
    break;
  case Type::All:
    ret = All;
    break;
  }
  return ret;
}

} // namespace Server
} // namespace Envoy
