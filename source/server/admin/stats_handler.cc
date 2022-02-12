#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"

#include "source/common/common/empty_string.h"
#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/utils.h"

constexpr uint64_t ChunkSize = 2 * 1000 * 1000;

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

class StatsHandler::Render {
public:
  virtual ~Render() = default;
  virtual void generate(Buffer::Instance& response, const std::string& name, uint64_t value) PURE;
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const std::string& value) PURE;
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const Stats::ParentHistogram& histogram) PURE;
  virtual void render(Buffer::Instance& response) PURE;
  virtual bool nextChunk(Buffer::Instance& response) PURE;
};

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
  bool nextChunk(Buffer::Instance& response) override { return response.length() > ChunkSize; }
};

class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  JsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
    response.add("{\"stats\":[");
  }

  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override {
    add(response, name, ValueUtil::numberValue(value));
  }
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override {
    add(response, name, ValueUtil::stringValue(value));
  }
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

  void render(Buffer::Instance& response) override {
    // emitStats(response);
    if (found_used_histogram_) {
      auto* histograms_obj_fields = histograms_obj_.mutable_fields();
      (*histograms_obj_fields)["computed_quantiles"] =
          ValueUtil::listValue(computed_quantile_array_);
      auto* histograms_obj_container_fields = histograms_obj_container_.mutable_fields();
      (*histograms_obj_container_fields)["histograms"] = ValueUtil::structValue(histograms_obj_);
      auto str = MessageUtil::getJsonStringFromMessageOrDie(
          ValueUtil::structValue(histograms_obj_container_), false /* pretty */, true);
      // stats_array_.push_back();
      // ENVOY_LOG_MISC(error, "histograms: {}", str);
      // response.addFragments({"  ", str, "\n"});
      addStatJson(response, str);
    }

    // auto* document_fields = document_.mutable_fields();
    //(*document_fields)["stats"] = ValueUtil::listValue(stats_array_);
    // response.add(MessageUtil::getJsonStringFromMessageOrDie(document_, false /* pretty */,
    // true));
    response.add("]}");
  }

  bool nextChunk(Buffer::Instance& response) override {
    return response.length() > ChunkSize;
    /*
    if (++chunk_count_ == 10000) {
      emitStats(response);
      return true;
    }
    return false;
    */
  }

private:
  template <class Value>
  void add(Buffer::Instance& response, const std::string& name, const Value& value) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(name);
    (*stat_obj_fields)["value"] = value;
    // stats_array_.push_back(ValueUtil::structValue(stat_obj));
    auto str = MessageUtil::getJsonStringFromMessageOrDie(stat_obj, false /* pretty */, true);
    // ENVOY_LOG_MISC(error, "emitting: {}", str);
    // response.addFragments({"  ", str, ",\n"});
    addStatJson(response, str);
  }

  void emitStats(Buffer::Instance& response) {
    auto str = MessageUtil::getJsonStringFromMessageOrDie(ValueUtil::listValue(stats_array_),
                                                          false /* pretty */, true);
    // ENVOY_LOG_MISC(error, "emitting: {}", str);
    response.add(str);
    chunk_count_ = 0;
    stats_array_.clear();
  }

  void addStatJson(Buffer::Instance& response, const std::string& json) {
    if (first_) {
      response.add(json);
      first_ = false;
    } else {
      response.addFragments({",", json});
    }
  }

  uint32_t chunk_count_{0};
  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  std::vector<ProtobufWkt::Value> scope_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
  bool first_{true};
};

class StatsHandler::Context : public Admin::Handler {
  using ScopeVec = std::vector<Stats::ConstScopeSharedPtr>;
  using StatOrScopes =
      absl::variant<ScopeVec, Stats::TextReadoutSharedPtr, Stats::CounterSharedPtr,
      Stats::GaugeSharedPtr, Stats::HistogramSharedPtr>;
  enum class Phase {
    TextReadouts,
    CountersAndGauges,
    Histograms,
  };

 public:
  Context(Server::Instance& server, bool used_only, bool json, absl::optional<std::regex> regex)
      : used_only_(used_only), json_(json), regex_(regex), stats_(server.stats()) {}

  // Admin::Handler
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
        [this](const Stats::Scope& scope) { scopes_.emplace_back(scope.makeConstShared()); });

    startPhase();
    return Http::Code::OK;
  }

  bool nextChunk(Buffer::Instance& response) override {
    if (response_.length() > 0) {
      ASSERT(response.length() == 0);
      response.move(response_);
    }
    while (!render_->nextChunk(response)) {
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
          if (!skip(histogram, name)) {
            auto parent_histogram = dynamic_cast<Stats::ParentHistogram*>(histogram.get());
            if (parent_histogram != nullptr) {
              render_->generate(response, name, *parent_histogram);
            }
          }
        }
      }
      stat_map_.erase(iter);
    }
    return true;
  }
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

  template <class StatType> bool shouldShowMetric(const StatType& stat) {
    return StatsHandler::shouldShowMetric(stat, used_only_, regex_);
  }

  void populateStatsForCurrentPhase(const ScopeVec& scope_vec);
  template <class StatType> void populateStatsFromScopes(const ScopeVec& scope);
  template <class SharedStatType>
      void renderStat(const std::string& name, Buffer::Instance& response, StatOrScopes& variant);
  template <class SharedStatType> bool skip(const SharedStatType& stat, const std::string& name);
  const bool used_only_;
  const bool json_;
  absl::optional<std::regex> regex_;
  absl::optional<std::string> format_value_;

  std::unique_ptr<Render> render_;

  static constexpr uint32_t num_stats_per_chunk_ = 1000;
  Stats::Store& stats_;
  ScopeVec scopes_;
  using StatMap = std::map<std::string, StatOrScopes>;
  StatMap stat_map_;
  uint32_t stats_and_scopes_index_{0};
  uint32_t chunk_index_{0};
  Phase phase_{Phase::TextReadouts};
  Buffer::OwnedImpl response_;
};

Admin::HandlerPtr StatsHandler::makeContext(absl::string_view path, AdminStream& /*admin_stream*/) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  const Http::Utility::QueryParams params = Http::Utility::parseAndDecodeQueryString(path);

  const bool used_only = params.find("usedonly") != params.end();
  absl::optional<std::regex> regex;
  Buffer::OwnedImpl response;
  if (!Utility::filterParam(params, response, regex)) {
    return Admin::makeStaticTextHandler(response, Http::Code::BadRequest);
  }

  const absl::optional<std::string> format_value = Utility::formatParam(params);
  bool json = false;
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      Buffer::OwnedImpl response;
      Http::Code code = prometheusStats(path, response);
      return Admin::makeStaticTextHandler(response, code);
    } else if (format_value.value() == "json") {
      json = true;
    } else {
      return Admin::makeStaticTextHandler(
          "usage: /stats?format=json  or /stats?format=prometheus \n\n", Http::Code::BadRequest);
    }
  }

  return std::make_unique<Context>(server_, used_only, json, regex);
}

#if 0
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
  bool json = false;
  if (format_value.has_value()) {
    if (format_value.value() == "prometheus") {
      return handlerPrometheusStats(url, response_headers, response, admin_stream);
    }
    if (format_value.value() == "json") {
      json = true;
    } else {
      response.add("usage: /stats?format=json  or /stats?format=prometheus \n");
      response.add("\n");
      return Http::Code::BadRequest;
    }
  }

  auto iter = context_map_.find(&admin_stream);
  if (iter == context_map_.end()) {
    auto insertion = context_map_.emplace(
        &admin_stream,
        std::make_unique<Context>(server_, used_only, regex, json, response_headers, response));
    iter = insertion.first;
  }
  Context& context = *iter->second;

  Http::Code code = context.eChunk(response);
  if (code != Http::Code::Continue) {
    context_map_.erase(iter);
  }

  return code;
}
#endif

void StatsHandler::Context::populateStatsForCurrentPhase(const ScopeVec& scope_vec) {
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

template <class StatType>
void StatsHandler::Context::populateStatsFromScopes(const ScopeVec& scope_vec) {
  for (const Stats::ConstScopeSharedPtr& scope : scope_vec) {
    Stats::IterateFn<StatType> fn = [this](const Stats::RefcountPtr<StatType>& stat) -> bool {
      stat_map_[stat->name()] = stat;
      return true;
    };
    scope->iterate(fn);
  }
}

template <class SharedStatType>
void StatsHandler::Context::renderStat(const std::string& name, Buffer::Instance& response,
                                       StatOrScopes& variant) {
  auto stat = absl::get<SharedStatType>(variant);
  if (skip(stat, name)) {
    return;
  }
  render_->generate(response, name, stat->value());
}

template <class SharedStatType>
bool StatsHandler::Context::skip(const SharedStatType& stat, const std::string& name) {
  if (used_only_ && !stat->used()) {
    return true;
  }
  if (regex_.has_value() && !std::regex_search(name, regex_.value())) {
    return true;
  }
  return false;
}

#if 0
struct CompareStatsAndScopes {
  CompareStatsAndScopes(Stats::SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  bool operator()(const StatOrScope& a, const StatOrScope& b) const {
    return symbol_table_.lessThan(name(a), name(b));
  }

  Stats::StatName name(const StatOrScope& stat_or_scope) const {
    switch (stat_or_scope.index()) {
      case 0: return absl::get<Stats::ConstScopeSharedPtr>(stat_or_scope)->prefix();
      case 1: return absl::get<Stats::TextReadoutSharedPtr>(stat_or_scope)->statName();
      case 2: return absl::get<Stats::CounterSharedPtr>(stat_or_scope)->statName();
      case 3: return absl::get<Stats::GaugeSharedPtr>(stat_or_scope)->statName();
      case 4: return absl::get<Stats::ParentHistogramSharedPtr>(stat_or_scope)->statName();
    }
    return Stats::StatName();
  }

  Stats::SymbolTable& symbol_table_;
};
#endif

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
