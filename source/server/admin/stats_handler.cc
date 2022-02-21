#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/utils.h"

#include "absl/container/btree_map.h"
#include "absl/types/variant.h"

namespace {
constexpr uint64_t ChunkSize = 2 * 1000 * 1000;
} // namespace

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

// Abstract class for rendering stats. Every method is called "generate"
// differing only by the data type, to facilitate templatized call-sites.
//
// There are currently Json and Text implementations of this interface, and in
// #19546 an HTML version will be added to provide a hierarchical view.
class StatsHandler::Render {
public:
  virtual ~Render() = default;

  // Writes a fragment for a numeric value, for counters and gauges.
  virtual void generate(Buffer::Instance& response, const std::string& name, uint64_t value) PURE;

  // Writes a json fragment for a textual value, for text readouts.
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const std::string& value) PURE;

  // Writes a histogram value.
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const Stats::ParentHistogram& histogram) PURE;

  // Completes rendering any buffered data.
  virtual void render(Buffer::Instance& response) PURE;

  // Determines whether the current chunk is full.
  bool isChunkFull(Buffer::Instance& response) { return response.length() > ChunkSize; }
};

// Implements the Render interface for simple textual representation of stats.
class StatsHandler::TextRender : public StatsHandler::Render {
public:
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override {
    response.addFragments({name, ": ", absl::StrCat(value), "\n"});
  }

  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override {
    response.addFragments({name, ": \"", Html::Utility::sanitize(value), "\"\n"});
  }

  void generate(Buffer::Instance& response, const std::string& name,
                const Stats::ParentHistogram& histogram) override {
    response.addFragments({name, ": ", histogram.quantileSummary(), "\n"});
  }

  void render(Buffer::Instance&) override {}
};

// Implements the Render interface for json output.
class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  JsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    // We don't create a JSON data model for the entire stats output, as that
    // makes streaming difficult. Instead we emit the preamble in the
    // constructor here, and create json models for each stats entry.
    response.add("{\"stats\":[");
  }

  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override {
    add(response, name, ValueUtil::numberValue(value));
  }

  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override {
    add(response, name, ValueUtil::stringValue(value));
  }

  // In JSON we buffer all histograms and don't write them immediately, so we
  // can, in one JSON structure, emit shared attributes of all histograms and
  // each individual histogram.
  //
  // This is counter to the goals of streaming and chunked interfaces, but
  // usually there are far fewer histograms than counters or gauges.
  void generate(Buffer::Instance&, const std::string& name,
                const Stats::ParentHistogram& histogram) override {
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
    for (size_t i = 0; i < histogram.intervalStatistics().supportedQuantiles().size(); ++i) {
      ProtobufWkt::Struct computed_quantile_value;
      auto* computed_quantile_value_fields = computed_quantile_value.mutable_fields();
      const auto& interval = histogram.intervalStatistics().computedQuantiles()[i];
      const auto& cumulative = histogram.cumulativeStatistics().computedQuantiles()[i];
      (*computed_quantile_value_fields)["interval"] =
          std::isnan(interval) ? ValueUtil::nullValue() : ValueUtil::numberValue(interval);
      (*computed_quantile_value_fields)["cumulative"] =
          std::isnan(cumulative) ? ValueUtil::nullValue() : ValueUtil::numberValue(cumulative);

      computed_quantile_value_array.push_back(ValueUtil::structValue(computed_quantile_value));
    }
    (*computed_quantile_fields)["values"] = ValueUtil::listValue(computed_quantile_value_array);
    computed_quantile_array_.push_back(ValueUtil::structValue(computed_quantile));
  }

  // Since histograms are buffered (see above), the render() method generates
  // all of them.
  void render(Buffer::Instance& response) override {
    if (found_used_histogram_) {
      auto* histograms_obj_fields = histograms_obj_.mutable_fields();
      (*histograms_obj_fields)["computed_quantiles"] =
          ValueUtil::listValue(computed_quantile_array_);
      auto* histograms_obj_container_fields = histograms_obj_container_.mutable_fields();
      (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj_);
      auto str = MessageUtil::getJsonStringFromMessageOrDie(
          ValueUtil::structValue(histograms_obj_container_), false /* pretty */, true);
      addStatJson(response, str);
    }
    response.add("]}");
  }

private:
  template <class Value>
  void add(Buffer::Instance& response, const std::string& name, const Value& value) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(name);
    (*stat_obj_fields)["value"] = value;
    auto str = MessageUtil::getJsonStringFromMessageOrDie(stat_obj, false /* pretty */, true);
    addStatJson(response, str);
  }

  void addStatJson(Buffer::Instance& response, const std::string& json) {
    if (first_) {
      response.add(json);
      first_ = false;
    } else {
      response.addFragments({",", json});
    }
  }

  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  std::vector<ProtobufWkt::Value> scope_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
  bool first_{true};
};

// Captures context for a streaming request, implementing the AdminHandler interface.
class StatsHandler::StreamingRequest : public Admin::Request {
  using ScopeVec = std::vector<Stats::ConstScopeSharedPtr>;
  using StatOrScopes = absl::variant<ScopeVec, Stats::TextReadoutSharedPtr, Stats::CounterSharedPtr,
                                     Stats::GaugeSharedPtr, Stats::HistogramSharedPtr>;
  enum class Phase {
    TextReadouts,
    CountersAndGauges,
    Histograms,
  };

public:
  StreamingRequest(Stats::Store& stats, bool used_only, bool json, absl::optional<std::regex> regex)
      : used_only_(used_only), json_(json), regex_(regex), stats_(stats) {}

  // Admin::Request
  Http::Code start(Http::ResponseHeaderMap& response_headers) override {
    if (json_) {
      render_ = std::make_unique<JsonRender>(response_headers, response_);
    } else {
      render_ = std::make_unique<TextRender>();
    }

    // Populate the top-level scopes and the stats underneath any scopes with an empty name.
    // We will have to de-dup, but we can do that after sorting.
    //
    // First capture all the scopes and hold onto them with a SharedPtr so they
    // can't be deleted after the initial iteration.
    stats_.forEachScope(
        [this](size_t s) { scopes_.reserve(s); },
        [this](const Stats::Scope& scope) { scopes_.emplace_back(scope.getConstShared()); });

    startPhase();
    return Http::Code::OK;
  }

  // nextChunkI() Streams out the next chunk of stats to the client, visiting
  // only the scopes that can plausibly contribute the next set of named
  // stats. This enables us to linearly traverse the entire set of stats without
  // buffering all of them and sorting.
  //
  // Instead we keep the a set of candidate stats to emit in stat_map_ an
  // alphabetically ordered btree, which heterogeneously stores stats of all
  // types and scopes. Note that there can be multiple scopes with the same
  // name, so we keep same-named scopes in a vector. However leaf metrics cannot
  // have duplicates. It would also be feasible to use a multi-map for this.
  //
  // So in start() above, we initially populate all the scopes, as well as the
  // metrics contained in all scopes with an empty name. So in nextChunk we can
  // emit and remove the first element of stat_map_. When we encounter a vector
  // of scopes then we add the contained metrics to the map and continue
  // iterating.
  //
  // Whenever the desired chunk size is reached we end the current chunk so that
  // the current buffer can be flushed to the network. In #19898 we will
  // introduce flow-control so that we don't buffer the all the serialized stats
  // while waiting for a slow client.
  //
  // Note that we do 3 passes through all the scopes_, so that we can emit
  // text-readouts first, then the intermingled counters and gauges, and finally
  // the histograms.
  bool nextChunk(Buffer::Instance& response) override {
    if (response_.length() > 0) {
      ASSERT(response.length() == 0);
      response.move(response_);
      ASSERT(response_.length() == 0);
    }
    while (!render_->isChunkFull(response)) {
      while (stat_map_.empty()) {
        switch (phase_) {
        case Phase::TextReadouts:
          phase_ = Phase::CountersAndGauges;
          startPhase();
          break;
        case Phase::CountersAndGauges:
          phase_ = Phase::Histograms;
          startPhase();
          break;
        case Phase::Histograms:
          render_->render(response);
          return false;
        }
      }

      auto iter = stat_map_.begin();
      const std::string& name = iter->first;
      StatOrScopes& variant = iter->second;
      switch (variant.index()) {
      case 0:
        populateStatsForCurrentPhase(absl::get<ScopeVec>(variant));
        break;
      case 1:
        renderStat<Stats::TextReadoutSharedPtr>(name, response, variant);
        break;
      case 2:
        renderStat<Stats::CounterSharedPtr>(name, response, variant);
        break;
      case 3:
        renderStat<Stats::GaugeSharedPtr>(name, response, variant);
        break;
      case 4: {
        auto histogram = absl::get<Stats::HistogramSharedPtr>(variant);
        //if (!skip(histogram, name)) {
          auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
          if (parent_histogram != nullptr) {
            render_->generate(response, name, *parent_histogram);
          }
          //}
      }
      }
      stat_map_.erase(iter);
    }
    return true;
  }

  // To duplicate prior behavior for this class, we do three passes over all the stats:
  //   1. text readouts across all scopes
  //   2. counters and gauges, co-mingled, across all scopes
  //   3. histograms across all scopes.
  // It would be little more efficient to co-mingle all the stats, but three
  // passes over the scopes is OK. In the future we may decide to organize the
  // result data differently, but in the process of changing from buffering
  // the entire /stats response to streaming the data out in chunks, it's easier
  // to reason about if the tests don't change their expectations.
  void startPhase() {
    ASSERT(stat_map_.empty());
    for (const Stats::ConstScopeSharedPtr& scope : scopes_) {
      StatOrScopes& variant = stat_map_[stats_.symbolTable().toString(scope->prefix())];
      if (variant.index() == absl::variant_npos) {
        variant = ScopeVec();
      }
      absl::get<ScopeVec>(variant).emplace_back(scope);
    }

    // Populate stat_map with all the counters found in all the scopes with an
    // empty prefix.
    auto iter = stat_map_.find("");
    if (iter != stat_map_.end()) {
      StatOrScopes variant = std::move(iter->second);
      stat_map_.erase(iter);
      auto& scope_vec = absl::get<ScopeVec>(variant);
      populateStatsForCurrentPhase(scope_vec);
    }
  }

  // Iterates over scope_vec and populates the metric types associated with the
  // current phase.
  void populateStatsForCurrentPhase(const ScopeVec& scope_vec) {
    switch (phase_) {
    case Phase::TextReadouts:
      populateStatsFromScopes<Stats::TextReadout>(scope_vec);
      break;
    case Phase::CountersAndGauges:
      populateStatsFromScopes<Stats::Counter>(scope_vec);
      populateStatsFromScopes<Stats::Gauge>(scope_vec);
      break;
    case Phase::Histograms:
      populateStatsFromScopes<Stats::Histogram>(scope_vec);
      break;
    }
  }

  // Populates all the metrics of the templatized type from scope_vec. Here we
  // exploit that Scope::iterate is a generic templatized function to avoid code
  // duplication.
  template <class StatType> void populateStatsFromScopes(const ScopeVec& scope_vec) {
    for (const Stats::ConstScopeSharedPtr& scope : scope_vec) {
      Stats::IterateFn<StatType> fn = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
        if (used_only_ && !stat->used()) {
          return true;
        }
        std::string name = stat->name();
        if (regex_.has_value() && !std::regex_search(name, regex_.value())) {
          return true;
        }
        stat_map_[name] = stat;
        return true;
      };
      scope->iterate(fn);
    }
  }

  // Renders the templatized type, exploiting the fact that Render::generate is
  // generic to avoid code duplication.
  template <class SharedStatType>
  void renderStat(const std::string& name, Buffer::Instance& response, StatOrScopes& variant) {
    auto stat = absl::get<SharedStatType>(variant);
    render_->generate(response, name, stat->value());
  }

private:
  const bool used_only_;
  const bool json_;
  absl::optional<std::regex> regex_;
  absl::optional<std::string> format_value_;

  std::unique_ptr<Render> render_;

  Stats::Store& stats_;
  ScopeVec scopes_;
  absl::btree_map<std::string, StatOrScopes> stat_map_;
  Phase phase_{Phase::TextReadouts};
  Buffer::OwnedImpl response_;
};

Admin::RequestPtr StatsHandler::makeRequest(absl::string_view path, AdminStream& /*admin_stream*/) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  const Http::Utility::QueryParams params = Http::Utility::parseAndDecodeQueryString(path);

  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  Buffer::OwnedImpl response;
  if (!Utility::filterParam(params, response, regex)) {
    return Admin::makeStaticTextRequest(response, Http::Code::BadRequest);
  }

  const absl::optional<std::string> format_value = Utility::formatParam(params);
  bool json = false;
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      Buffer::OwnedImpl response;
      Http::Code code = prometheusStats(path, response);
      return Admin::makeStaticTextRequest(response, code);
    } else if (format_value.value() == "json") {
      json = true;
    } else {
      return Admin::makeStaticTextRequest(
          "usage: /stats?format=json  or /stats?format=prometheus \n\n", Http::Code::BadRequest);
    }
  }

  return makeRequest(server_.stats(), used_only, json, regex);
}

Admin::RequestPtr StatsHandler::makeRequest(Stats::Store& stats, bool used_only, bool json,
                                            const absl::optional<std::regex>& regex) {
  return std::make_unique<StreamingRequest>(stats, used_only, json, regex);
}

Http::Code StatsHandler::handlerPrometheusStats(absl::string_view path_and_query,
                                                Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  return prometheusStats(path_and_query, response);
}

Http::Code StatsHandler::prometheusStats(absl::string_view path_and_query,
                                         Buffer::Instance& response) {
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

} // namespace Server
} // namespace Envoy
