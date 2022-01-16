#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
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
                                      Buffer::Instance& response, AdminStream&) {
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

  return stats(params, server_.stats(), response_headers, response);
}

class StatsHandler::Render {
public:
  virtual ~Render() = default;
  virtual void generate(Stats::Counter&) PURE;
  virtual void generate(Stats::Gauge&) PURE;
  virtual void generate(Stats::TextReadout&) PURE;
  virtual void generate(Stats::Histogram&) PURE;
  virtual void noStats(Type type) { UNREFERENCED_PARAMETER(type); }
  virtual void render(Buffer::Instance& response) PURE;
};

class StatsHandler::TextRender : public StatsHandler::Render {
protected:
  using Group = std::vector<std::string>;

public:
  void generate(Stats::TextReadout& text_readout) override {
    groups_[Type::TextReadouts].emplace_back(absl::StrCat(
        text_readout.name(), ": \"", Html::Utility::sanitize(text_readout.value()), "\"\n"));
  }
  void generate(Stats::Counter& counter) override {
    groups_[Type::Counters].emplace_back(absl::StrCat(counter.name(), ": ", counter.value(), "\n"));
  }
  void generate(Stats::Gauge& gauge) override {
    groups_[Type::Gauges].emplace_back(absl::StrCat(gauge.name(), ": ", gauge.value(), "\n"));
  }
  void generate(Stats::Histogram& histogram) override {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(&histogram);
    if (phist != nullptr) {
      groups_[Type::Histograms].emplace_back(
          absl::StrCat(phist->name(), ": ", phist->quantileSummary(), "\n"));
    }
  }

  void render(Buffer::Instance& response) override {
    for (const auto& iter : groups_) {
      const Group& group = iter.second;
      for (const std::string& str : group) {
        response.add(str);
      }
    }
  }

protected:
  std::map<Type, Group> groups_; // Ordered by Type's numeric value.
};

class StatsHandler::HtmlRender : public StatsHandler::TextRender {
public:
  HtmlRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
             StatsHandler& stats_handler, const Params& params)
      : response_(response), html_(response) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
    html_.setSubmitOnChange(true);
    html_.renderHead();
    html_.renderTableBegin();
    html_.renderUrlHandler(stats_handler.statsHandler(), params.query_);
    html_.renderInput("scope", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    html_.renderTableEnd();
  }

  ~HtmlRender() override { response_.add("</body>\n"); }

  void noStats(Type type) override {
    groups_[type]; // Make an empty group for this type.
  }

  void render(Buffer::Instance&) override {
    for (const auto& iter : groups_) {
      absl::string_view label = StatsHandler::typeToString(iter.first);
      const Group& group = iter.second;
      if (group.empty()) {
        response_.add(absl::StrCat("<br/><i>No ", label, " found</i><br/>\n"));
      } else {
        response_.add(absl::StrCat("<h1>", label, "</h1>\n<pre>\n"));
        for (const std::string& str : group) {
          response_.add(str);
        }
        response_.add("</pre>\n");
      }
    }
  }

private:
  Buffer::Instance& response_;
  AdminHtmlGenerator html_;
};

class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  explicit JsonRender(const Params& params) : params_(params) {}

  void generate(Stats::Counter& counter) override {
    add(counter, ValueUtil::numberValue(counter.value()));
  }
  void generate(Stats::Gauge& gauge) override { add(gauge, ValueUtil::numberValue(gauge.value())); }
  void generate(Stats::TextReadout& text_readout) override {
    add(text_readout, ValueUtil::stringValue(text_readout.value()));
  }
  void generate(Stats::Histogram& histogram) override {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(&histogram);
    if (phist != nullptr) {
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
      (*computed_quantile_fields)["name"] = ValueUtil::stringValue(histogram.name());

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
      computed_quantile_array_.push_back(ValueUtil::structValue(computed_quantile));
    }
  }

  void render(Buffer::Instance& response) override {
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
    response.add(MessageUtil::getJsonStringFromMessageOrDie(document_, params_.pretty_, true));
  }

private:
  template <class StatType, class Value> void add(StatType& stat, const Value& value) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(stat.name());
    (*stat_obj_fields)["value"] = value;
    stats_array_.push_back(ValueUtil::structValue(stat_obj));
  }

  const StatsHandler::Params& params_;
  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  std::vector<ProtobufWkt::Value> scope_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
};

class StatsHandler::Context {
public:
  // We need to hold ref-counts to each stat in our intermediate sets to avoid
  // having the stats be deleted while we are computing results.
  template <class StatType> struct Hash {
    size_t operator()(const Stats::RefcountPtr<StatType>& a) const { return a->statName().hash(); }
  };

  template <class StatType> struct Compare {
    bool operator()(const Stats::RefcountPtr<StatType>& a,
                    const Stats::RefcountPtr<StatType>& b) const {
      return a->statName() == b->statName();
    }
  };

  template <class StatType>
  using SharedStatSet =
      absl::flat_hash_set<Stats::RefcountPtr<StatType>, Hash<StatType>, Compare<StatType>>;

  Context(const Params& params, Render& render, Buffer::Instance& response)
      : params_(params), render_(render), response_(response) {}

  void collectStats(const Stats::Store& stats) {
    collect<Stats::TextReadout>(Type::TextReadouts, stats, text_readouts_);
    collect<Stats::Counter>(Type::Counters, stats, counters_);
    collect<Stats::Gauge>(Type::Gauges, stats, gauges_);
    collect<Stats::Histogram>(Type::Histograms, stats, histograms_);
  }

  void emit() {
    emit<Stats::TextReadout>(Type::TextReadouts, text_readouts_);
    emit<Stats::Counter>(Type::Counters, counters_);
    emit<Stats::Gauge>(Type::Gauges, gauges_);
    emit<Stats::Histogram>(Type::Histograms, histograms_);
  }

  template <class StatType>
  void collect(Type type, const Stats::Store& stats, SharedStatSet<StatType>& set) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    collectHelper(stats, [this, &set](StatType& stat) {
      if (params_.shouldShowMetric(stat)) {
        set.insert(&stat);
      }
      return true;
    });
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::TextReadout&> fn) {
    for (const Stats::TextReadoutSharedPtr& text_readout : stats.textReadouts()) {
      fn(*text_readout);
    }
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::Counter&> fn) {
    for (const Stats::CounterSharedPtr& counter : stats.counters()) {
      fn(*counter);
    }
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::Gauge&> fn) {
    for (const Stats::GaugeSharedPtr& gauge : stats.gauges()) {
      fn(*gauge);
    }
  }

  void collectHelper(const Stats::Store& stats, Stats::StatFn<Stats::Histogram&> fn) {
    for (const Stats::ParentHistogramSharedPtr& histogram : stats.histograms()) {
      fn(*histogram);
    }
  }

  template <class StatType> void emit(Type type, SharedStatSet<StatType>& set) {
    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    if (set.empty()) {
      render_.noStats(type);
    }

    std::vector<Stats::RefcountPtr<StatType>> sorted;
    sorted.reserve(set.size());
    for (const Stats::RefcountPtr<StatType>& stat : set) {
      sorted.emplace_back(stat);
    }

    struct Cmp {
      bool operator()(const Stats::RefcountPtr<StatType>& a,
                      const Stats::RefcountPtr<StatType>& b) const {
        return a->constSymbolTable().lessThan(a->statName(), b->statName());
      }
    };
    std::sort(sorted.begin(), sorted.end(), Cmp());

    for (const Stats::RefcountPtr<StatType>& stat : sorted) {
      render_.generate(*stat);
    }
  }

  const Params& params_;
  Render& render_;
  Buffer::Instance& response_;

  SharedStatSet<Stats::Counter> counters_;
  SharedStatSet<Stats::Gauge> gauges_;
  SharedStatSet<Stats::TextReadout> text_readouts_;
  SharedStatSet<Stats::Histogram> histograms_;
  std::set<std::string> scopes_;
};

Http::Code StatsHandler::stats(const Params& params, Stats::Store& stats,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  std::unique_ptr<Render> render;

  switch (params.format_) {
  case Format::Html:
    render = std::make_unique<HtmlRender>(response_headers, response, *this, params);
    break;
  case Format::Json:
    render = std::make_unique<JsonRender>(params);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    break;
  case Format::Prometheus:
    ASSERT(false);
    ABSL_FALLTHROUGH_INTENDED;
  case Format::Text:
    render = std::make_unique<TextRender>();
    break;
  }

  Context context(params, *render, response);
  context.collectStats(stats);
  context.emit();
  render->render(response);

  if (params.format_ == Format::Html) {
    response.add("</body>\n");
  }

  // Display plain stats if format query param is not there.
  // statsAsText(counters_and_gauges, text_readouts, histograms, response);
  return Http::Code::OK;
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
