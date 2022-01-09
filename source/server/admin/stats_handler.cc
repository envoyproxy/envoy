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

  Http::Utility::QueryParams::const_iterator pagesize_iter = query_.find("pagesize");
  if (pagesize_iter != query_.end()) {
    // We don't accept arbitrary page sizes as they might be dangerous.
    uint32_t page_size = 0;
    if (pagesize_iter->second == "unlimited") {
      page_size_ = absl::nullopt;
    } else if (!absl::SimpleAtoi(pagesize_iter->second, &page_size) || page_size > 1000) {
      response.add("pagesize invalid -- must be <= 1000 or unlimited");
      return Http::Code::BadRequest;
    }
    page_size_ = page_size;
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

  auto set_param = [this](const std::string& name, std::function<void(const std::string&)> setter) {
    auto iter = query_.find(name);
    if (iter != query_.end()) {
      setter(iter->second);
    }
  };

  set_param("scope", [this](const std::string& val) { scope_ = val; });

#if 0
  auto parse_start = [this, parse_type](absl::string_view val) -> bool {
    std::vector<absl::string_view> split = absl::StrSplit(val, absl::MaxSplits(StartSeparator, 1));
    if ((split.size() != 2) || !parse_type(split[0], start_type_)) {
      return false;
    }
    start_ = std::string(split[1]);
    return true;
  };

  // For clarity and brevity of command-line options, we parse &after=foo to
  // mean display the first page of stats alphabetically after "foo", and
  // &&before=bar to mean display the last page of stats alphabetically before
  // "bar". It is not valid to specify both &before=... and after, though that
  // could make sense in principle. It's just not that useful for paging, so
  // it is not implemented. If nothing is specified, that implies "&after=",
  // which gives you the first page.
  Http::Utility::QueryParams::const_iterator after_iter = query_.find("after");
  Http::Utility::QueryParams::const_iterator before_iter = query_.find("before");
  if (before_iter != query_.end() && !before_iter->second.empty()) {
    if (after_iter != query_.end() && !after_iter->second.empty()) {
      response.add("Only one of &before= and &after= is allowed");
      return Http::Code::BadRequest;
    }
    if (!parse_start(before_iter->second)) {
      response.add("bad before= param");
      return Http::Code::BadRequest;
    }
    direction_ = Stats::PageDirection::Backward;
  } else {
    direction_ = Stats::PageDirection::Forward;
    if (after_iter != query_.end() && !after_iter->second.empty() &&
        !parse_start(after_iter->second)) {
      response.add("bad after= param");
      return Http::Code::BadRequest;
    }
  }
#endif

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

Http::Code StatsHandler::handlerStatsJson(absl::string_view url,
                                          Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream&) {
  Params params;
  Http::Code code = params.parse(url, response);
  if (code != Http::Code::OK) {
    return code;
  }
  params.format_ = Format::Json;
  return stats(params, server_.stats(), response_headers, response);
}

class StatsHandler::Render {
public:
  virtual ~Render() = default;
  virtual void generate(Stats::Counter&) PURE;
  virtual void generate(Stats::Gauge&) PURE;
  virtual void generate(Stats::TextReadout&) PURE;
  virtual void generate(Stats::Histogram&) PURE;
  virtual void addScope(const std::string&) {}
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
  void noStats(Type type) override {
    groups_[type]; // Make an empty group for this type.
  }

  void render(Buffer::Instance& response) override {
    for (const auto& iter : groups_) {
      absl::string_view label = StatsHandler::typeToString(iter.first);
      //ENVOY_LOG_MISC(error, "Rendering {}", label);
      const Group& group = iter.second;
      if (group.empty()) {
        response.add(absl::StrCat("<br/><i>No ", label, " found</i><br/>\n"));
      } else {
        response.add(absl::StrCat("<h1>", label, "</h1>\n<pre>\n"));
        for (const std::string& str : group) {
          response.add(str);
        }
        response.add("</pre>\n");
      }
    }
  }
};

class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  explicit JsonRender(const Params& params) : params_(params) {}

  void generate(Stats::Counter& counter) override {
    add(counter, ValueUtil::numberValue(counter.value()));
  }
  void generate(Stats::Gauge& gauge) override {
    add(gauge, ValueUtil::numberValue(gauge.value()));
  }
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

  void addScope(const std::string& prefix) override {
    scope_array_.push_back(ValueUtil::stringValue(prefix));
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
    (*document_fields)["scopes"] = ValueUtil::listValue(scope_array_);
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
  Context(const Params& params, Render& render, Buffer::Instance& response)
      : params_(params), render_(render), response_(response) {}

  void collectScope(const Stats::Scope& scope) {
    collect<Stats::TextReadout>(Type::TextReadouts, scope, text_readouts_);
    collect<Stats::Counter>(Type::Counters, scope, counters_);
    collect<Stats::Gauge>(Type::Gauges, scope, gauges_);
    collect<Stats::Histogram>(Type::Histograms, scope, histograms_);
  }

  void collectScopePrefixes(const Stats::Scope& scope) {
    collectPrefixes<Stats::TextReadout>(Type::TextReadouts, scope);
    collectPrefixes<Stats::Counter>(Type::Counters, scope);
    collectPrefixes<Stats::Gauge>(Type::Gauges, scope);
    collectPrefixes<Stats::Histogram>(Type::Histograms, scope);
  }

  void emit() {
    emit<Stats::TextReadout>(Type::TextReadouts, text_readouts_);
    emit<Stats::Counter>(Type::Counters, counters_);
    emit<Stats::Gauge>(Type::Gauges, gauges_);
    emit<Stats::Histogram>(Type::Histograms, histograms_);
    emitScopes();
  }

  template<class StatType> void collect(Type type, const Stats::Scope& scope,
                                        Stats::StatSet<StatType>& set) {
    //ENVOY_LOG_MISC(error, "collect {}", typeToString(type));

    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    Stats::IterateFn<StatType> fn = [this, &set](const Stats::RefcountPtr<StatType>& stat) -> bool {
      if (params_.shouldShowMetric(*stat)) {
        set.insert(stat.get());
      }
      return true;
    };
    scope.iterate(fn);
  }

  template<class StatType> void collectPrefixes(Type type, const Stats::Scope& scope) {
    //ENVOY_LOG_MISC(error, "collect {}", typeToString(type));

    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    Stats::IterateFn<StatType> fn = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
      if (params_.shouldShowMetric(*stat)) {
        std::string name = stat->name();
        std::string::size_type dot = name.find('.');
        if (dot != std::string::npos) {
          name.resize(dot);
          prefixes_.insert(name);
        }
      }
      return true;
    };
    scope.iterate(fn);
  }

  template <class StatType> void emit(Type type, Stats::StatSet<StatType>& set) {
    //ENVOY_LOG_MISC(error, "emit {}", typeToString(type));

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

  void emitScopes() {
    for (const std::string& scope : scopes_) {
      render_.addScope(scope);
    }
  }

#if 0
  template <class StatType>
      void emit(Type type /*, std::function<void(StatType& stat_type)> render_fn */,

    //ENVOY_LOG_MISC(error, "emit {}", typeToString(type));

    // Bail early if the  requested type does not match the current type.
    if (params_.type_ != Type::All && params_.type_ != type) {
      return;
    }

    std::vector<Stats::RefcountPtr<StatType>> stats;
    scope_.iterate([this, &stats](Stats::RefcountPtr<StatType>& stat) {
      if (params_.shouldShowMetric(stat)) {
        stats.push_back(&stat);
      }
    });

    if (stats.empty()) {
      render_.noStats(type);
    }

    struct Cmp {
      bool operator()(const Stats::RefcountPtr<StatType>& a,
                      const Stats::RefcountPtr<StatType>& b) const {
        return a->constSymbolTable().lessThan(a->statName(), b->statName());
      }
    };

    std::sort(stats.begin(), stats.end());
    for (const auto& stat : stats) {
      //render_fn(*stat);
      render_.generate(*stat);
    }
  }

  void textReadouts() {
    emit<Stats::TextReadout>(Type::TextReadouts);
    // [this](Stats::TextReadout& text_readout) { render().textReadout(text_readout); });
  }

  void counters() {
    emit<Stats::Counter>(Type::Counters);

    /*
    emit<Stats::Counter>(
        Type::Counters, CountersLabel,
        [this](Stats::Counter& counter) { render().counter(counter); },
        [this](Stats::PageFn<Stats::Counter> render) -> bool {
          return scope_.iterate<Stats::Counter>(render, start(Type::Counters), params_.direction_);
        });
    */
  }

  void gauges() {
    emit<Stats::Gauge>(Type::Gauges);
    /*emit<Stats::Gauge>(
        Type::Gauges, GaugesLabel, [this](Stats::Gauge& gauge) { render().gauge(gauge); },
        [this](Stats::PageFn<Stats::Gauge> render) -> bool {
          return scope_.gaugePage(render, start(Type::Gauges), params_.direction_);
          });*/
  }

  void histograms() {
    emit<Stats::Histogram>(Type::Histograms);
        /*
        Type::Histograms, HistogramsLabel,
        [this](Stats::Histogram& histogram) { render().histogram(histogram); },
        [this](Stats::PageFn<Stats::Histogram> render) -> bool {
          return stats_.histogramPage(render, start(Type::Histograms), params_.direction_);
          });*/
  }
#endif

  void addScope(const std::string& scope) { scopes_.insert(scope); }


  Render& render() { return render_; }

  absl::string_view start(Type type) const {
    if (type == params_.start_type_) {
      return params_.start_;
    }
    return absl::string_view();
  }

  int64_t num_{0};
  const Params& params_;
  Render& render_;
  Buffer::Instance& response_;
  //Stats::Store& stats_;
  std::string next_start_;
  std::string prev_start_;
  Stats::StatSet<Stats::Counter> counters_;
  Stats::StatSet<Stats::Gauge> gauges_;
  Stats::StatSet<Stats::TextReadout> text_readouts_;
  Stats::StatSet<Stats::Histogram> histograms_;
  std::set<std::string> prefixes_;
  std::set<std::string> scopes_;
  //Stats::StatSet<Stats::CounterSharedPtr> counters_;
};

Http::Code StatsHandler::stats(const Params& params, Stats::Store& stats,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response) {
  std::unique_ptr<Render> render;

  switch (params.format_) {
  case Format::Html: {
    render = std::make_unique<HtmlRender>();
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
    AdminHtmlGenerator html(response);
    html.setVisibleSubmit(false);
    html.setSubmitOnChange(true);
    html.renderHead();
    html.renderUrlHandler(statsHandler(), params.query_);
    //html.renderInput("before", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    //html.renderInput("after", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    html.renderInput("scope", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    //html.renderInput("direction", "stats", Admin::ParamDescriptor::Type::Hidden, params.query_, {});
    html.renderTail();
    response.add("<body>\n");
    break;
  }
  case Format::Json:
    render = std::make_unique<JsonRender>(params);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    break;
  case Format::Prometheus:
    ASSERT(false);
    // fall through
  case Format::Text:
    render = std::make_unique<TextRender>();
    break;
  }

  Context context(params, *render, response);

  // If the scope is specified, then render all scopes that match it.
  if (params.scope_.has_value()) {
    // Note that multiple scopes can exist with the same name, and also that
    // scopes may be created/destroyed in other threads, whenever we are not
    // holding the store's lock. So when we traverse the scopes, we'll both
    // look for Scope objects matchiung our expected name, and we'll create
    // a sorted list of sort names for use in populating next/previous buttons.
    ASSERT(!params.scope_.value().empty());
    std::string params_scope = params.scope_.value();
    stats.forEachScope([](size_t) {}, [this, &params_scope, &context](const Stats::Scope& scope) {
      std::string scope_prefix_str = server_.stats().symbolTable().toString(scope.prefix());

      // If the scope matches the prefix of what the user wants, append in the
      // stats from it. Note that scopes with a prefix of "" will match anything
      // the user types, in which case we'll still be filtering based on stat name
      // prefix.
      if (scope_prefix_str == params_scope || scope_prefix_str.empty()) {
        ENVOY_LOG_MISC(error, "Collecting {}", scope_prefix_str);
        context.collectScope(scope);
      } else if (absl::StartsWith(scope_prefix_str, params_scope) &&
                 scope_prefix_str[params_scope.size()] == '.') {
        ENVOY_LOG_MISC(error, "Add sub-scope {}", scope_prefix_str);
        context.addScope(scope_prefix_str);
      }
    });
    context.emit();
    render->render(response);
  } else {
    // Get an ordered set of scope names, which are used for previous/next
    // navigation, and to choose a default scope name in case none was specified
    // in a query param.
    {
      //Stats::StatNameHashSet prefixes;
      Stats::StatFn<const Stats::Scope> add_scope = [this, &context](const Stats::Scope& scope) {
        //if (prefixes.insert(scope.prefix()).second) {
        std::string prefix_str = server_.stats().symbolTable().toString(scope.prefix());

        //#if 0
        // Truncate at dot. We are abstracting the scope so we
        std::string::size_type dot = prefix_str.find('.');
        if (dot != std::string::npos) {
          prefix_str.resize(dot);
        }
        //#endif

        // If the scope name is blank, then we need to find all the prefixes in
        // the scope by iterating over all the stats in it.
        if (prefix_str.empty()) {
          context.collectScopePrefixes(scope);
        } else {
          context.prefixes_.insert(prefix_str);
        }
        //}
      };
      server_.stats().forEachScope([](size_t) {}, add_scope);
    }
    for (const std::string& name : context.prefixes_) {
      response.add(absl::StrCat(
          "    <a href='javascript:expandScope(\"", name, "\")' id='scope_", name, "'>", name,
          "</a><br>\n"));
    }

#if 0
    // if the scope is unspecified, then render all the scopes.
    for (uint32_t i = 0; i < names.size(); ++i) {
      if (names[i] == scope_name) {
        found = true;
        if (i > 0) {
          prev = names[i - 1];
          ENVOY_LOG_MISC(error, "Found prev: {}", prev);
        }
        if (i < names.size() - 1) {
          next = names[i + 1];
          ENVOY_LOG_MISC(error, "Found next: {}", next);
        }
      }
    }
#endif
  }

  if (params.format_ == Format::Html) {
    /*
    if (!prev.empty()) {
      response.add(absl::StrCat("  <a href='javascript:setScope(\"", prev,
                                "\")'>Previous</a>\n"));
    }
    if (!next.empty()) {
      response.add(absl::StrCat("  <a href='javascript:setScope(\"", next, "\")'>Next</a>\n"));
    }
    */
    response.add("</body>\n");
  }

  // Display plain stats if format query param is not there.
  // statsAsText(counters_and_gauges, text_readouts, histograms, response);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsScopes(absl::string_view,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  std::string preamble = R"(<html>
  <head>
    <script>
      function visitScope(scope) {
        var params = "";
        if (document.getElementById("used").checked) {
          params += "&usedonly";
        }
        var filter = document.getElementById("filter").value;
        if (filter && filter.length > 0) {
          params += "&filter=" + filter;
        }
        location.href = "/stats?scope=" + scope + params;
      }
    </script>
  </head>
  <body>
    <label for="used">Used Only</label><input type="checkbox" id="used"><br>
    <label for="filter">Filter (regex)</label><input type="text" id="filter"><br>
)";

  Stats::StatNameHashSet prefixes;
  Stats::StatFn<const Stats::Scope> add_scope = [&prefixes](const Stats::Scope& scope) {
    prefixes.insert(scope.prefix());
  };
  server_.stats().forEachScope([](size_t) {}, add_scope);
  std::vector<std::string> lines, names;
  names.reserve(prefixes.size());
  lines.reserve(prefixes.size() + 2);
  lines.push_back(preamble);
  for (Stats::StatName prefix : prefixes) {
    names.emplace_back(server_.stats().symbolTable().toString(prefix));
  }
  std::sort(names.begin(), names.end());
  for (const std::string& name : names) {
    lines.push_back(
        absl::StrCat("    <a href='javascript:visitScope(\"", name, "\")'>", name, "</a><br>\n"));
  }
  lines.push_back("  </body>\n</html>\n");
  response.add(absl::StrJoin(lines, ""));
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsPrometheus(absl::string_view path_and_query,
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

void StatsHandler::statsAsText(const std::map<std::string, uint64_t>& counters_and_gauges,
                               const std::map<std::string, std::string>& text_readouts,
                               const std::vector<Stats::HistogramSharedPtr>& histograms,
                               Buffer::Instance& response) {
  // Display plain stats if format query param is not there.
  for (const auto& text_readout : text_readouts) {
    response.add(fmt::format("{}: \"{}\"\n", text_readout.first,
                             Html::Utility::sanitize(text_readout.second)));
  }
  for (const auto& stat : counters_and_gauges) {
    response.add(fmt::format("{}: {}\n", stat.first, stat.second));
  }
  for (const auto& histogram : histograms) {
    Stats::ParentHistogram* phist = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
    if (phist != nullptr) {
      response.add(fmt::format("{}: {}\n", phist->name(), phist->quantileSummary()));
    }
  }
}

std::string StatsHandler::statsAsJson(const std::map<std::string, uint64_t>& counters_and_gauges,
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
  for (const auto& stat : counters_and_gauges) {
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
            {"html", "text", "json", "prometheus"}},
           {Admin::ParamDescriptor::Type::Enum,
            "pagesize",
            "Number of stats to show per page..",
            {"25", "100", "1000", "unlimited"}},
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
