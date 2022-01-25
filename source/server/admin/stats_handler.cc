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
    auto insertion = context_map_.emplace(&admin_stream, std::make_unique<Context>(
        server_, used_only, regex, json, response_headers, response));
    iter = insertion.first;
  }
  Context& context = *iter->second;

  Http::Code code = context.writeChunk(response);
  if (code != Http::Code::Continue) {
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
  virtual void render() PURE;
};

Http::Code StatsHandler::Context::writeChunk(Buffer::Instance& response) {
  for (uint32_t start = response.length();
       stats_and_scopes_index_ < stats_and_scopes_.size() && (response.length() - start) < chunk_size_;
       ++stats_and_scopes_index_) {
    const StatOrScope& stat_or_scope = stats_and_scopes_[stats_and_scopes_index_];
    switch (stat_or_scope.index()) {
      case 0:
        //renderScope(absl::get<Stats::ScopeSharedPtr>(stat_or_scope));
        break;
      case 1: {
        auto text_readout = absl::get<Stats::TextReadoutSharedPtr>(stat_or_scope);
        render_->generate(text_readout->name(), text_readout->value());
        break;
      }
      case 2: {
        auto counter = absl::get<Stats::CounterSharedPtr>(stat_or_scope);
        render_->generate(counter->name(), counter->value());
        break;
      }
      case 3: {
        auto gauge = absl::get<Stats::GaugeSharedPtr>(stat_or_scope);
        render_->generate(gauge->name(), gauge->value());
        break;
      }
      case 4: {
        auto histogram = absl::get<Stats::ParentHistogramSharedPtr>(stat_or_scope);
        render_->generate(histogram->name(), histogram);
      }
    }
  }

  if (stats_and_scopes_index_ < stats_and_scopes_.size()) {
    return Http::Code::Continue;
  }
  return Http::Code::OK;

}

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

class StatsHandler::JsonRender : public StatsHandler::Render {
public:
  JsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response)
      : response_(response) {
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
    response_.add(MessageUtil::getJsonStringFromMessageOrDie(document_, false /* pretty */, true));
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
  ProtobufWkt::Struct document_;
  std::vector<ProtobufWkt::Value> stats_array_;
  std::vector<ProtobufWkt::Value> scope_array_;
  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::vector<ProtobufWkt::Value> computed_quantile_array_;
  bool found_used_histogram_{false};
};

StatsHandler::Context::Context(Server::Instance& server,
                               bool used_only, absl::optional<std::regex> regex,
                               bool json,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response)
    : used_only_(used_only), regex_(regex), stats_(server.stats()) {
  if (json) {
    render_ = std::make_unique<JsonRender>(response_headers, response);
  } else {
    render_ = std::make_unique<TextRender>(response);
  }

  // Populate the top-level scopes and the stats underneath any scopes with an empty name.
  // We will have to de-dup, but we can do that after sorting.
  stats_.forEachScope(nullptr, [this](const Stats::Scope& scope) {
    if (scope.prefix().empty()) {
      Stats::IterateFn<Stats::Counter> fn = [this](const Stats::CounterSharedPtr& counter) -> bool {
        stats_and_scopes_.emplace_back(counter);
        return true;
      };
      scope.iterate(fn);
    } else {
      stats_and_scopes_.emplace_back(Stats::ScopeSharedPtr(const_cast<Stats::Scope*>(&scope)));
    }
  });

  struct CompareStatsAndScopes {
    CompareStatsAndScopes(Stats::SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
    bool operator()(const StatOrScope& a, const StatOrScope& b) const {
      return symbol_table_.lessThan(name(a), name(b));
    }

    Stats::StatName name(const StatOrScope& stat_or_scope) const {
      switch (stat_or_scope.index()) {
        case 0: return absl::get<Stats::ScopeSharedPtr>(stat_or_scope)->prefix();
        case 1: return absl::get<Stats::TextReadoutSharedPtr>(stat_or_scope)->statName();
        case 2: return absl::get<Stats::CounterSharedPtr>(stat_or_scope)->statName();
        case 3: return absl::get<Stats::GaugeSharedPtr>(stat_or_scope)->statName();
        case 4: return absl::get<Stats::ParentHistogramSharedPtr>(stat_or_scope)->statName();
      }
      return Stats::StatName();
    }

    Stats::SymbolTable& symbol_table_;
  };


  std::sort(stats_and_scopes_.begin(), stats_and_scopes_.end(), CompareStatsAndScopes(stats_.symbolTable()));
  std::unique(stats_and_scopes_.begin(), stats_and_scopes_.end());

  /*
  for (const Stats::CounterSharedPtr& counter : server.stats().counters()) {
    if (shouldShowMetric(*counter)) {
      all_stats_.emplace(counter->name(), counter->value());
    }
  }

  for (const Stats::GaugeSharedPtr& gauge : server.stats().gauges()) {
    if (shouldShowMetric(*gauge)) {
      ASSERT(gauge->importMode() != Stats::Gauge::ImportMode::Uninitialized);
      all_stats_.emplace(gauge->name(), gauge->value());
    }
  }
  all_stats_iter_ = all_stats_.begin();

  for (const auto& text_readout : server.stats().textReadouts()) {
    if (shouldShowMetric(*text_readout)) {
      text_readouts_.emplace(text_readout->name(), text_readout->value());
    }
  }
  text_readouts_iter_ = text_readouts_.begin();

  histograms_ = server.stats().histograms();
  */
}

StatsHandler::Context::~Context() {
}

bool StatsHandler::Context::Context::nextChunk() {
  if (++chunk_index_ == chunk_size_) {
    chunk_index_= 0;
    return true;
  }
  return false;
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

Http::Code StatsHandler::Context::statsAsText(Http::ResponseHeaderMap& /*response_headers*/,
                                              Buffer::Instance& /*response*/) {
  /*
  // Display plain stats if format query param is not there.
  for (; text_readouts_iter_ != text_readouts_.end(); ++text_readouts_iter_) {
    if (nextChunk()) {
      return Http::Code::Continue;
    }
    response.addFragments(
        {text_readouts_iter_->first, ": \"", Html::Utility::sanitize(text_readouts_iter_->second),
         "\"\n"});
  }
  for (; all_stats_iter_ != all_stats_.end(); ++all_stats_iter_) {
    if (nextChunk()) {
      return Http::Code::Continue;
    }
    response.addFragments({all_stats_iter_->first, ": ", absl::StrCat(all_stats_iter_->second), "\n"});
  }
  std::map<std::string, std::string> all_histograms;
  for (const Stats::ParentHistogramSharedPtr& histogram : histograms_) {
    if (shouldShowMetric(*histogram)) {
      auto insert = all_histograms.emplace(histogram->name(), histogram->quantileSummary());
      ASSERT(insert.second); // No duplicates expected.
    }
  }
  for (const auto& histogram : all_histograms) {
    response.addFragments({histogram.first, ": ", histogram.second, "\n"});
  }
  return Http::Code::OK;
  */
  return Http::Code::OK;
}

Http::Code StatsHandler::Context::statsAsJson(Http::ResponseHeaderMap& /*response_headers*/,
                                              Buffer::Instance& /*response*/,
                                              const bool /*pretty_print*/) {

#if 0
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  ProtobufWkt::Struct document;
  std::vector<ProtobufWkt::Value> stats_array;
  for (const auto& text_readout : text_readouts_) {
    ProtobufWkt::Struct stat_obj;
    auto* stat_obj_fields = stat_obj.mutable_fields();
    (*stat_obj_fields)["name"] = ValueUtil::stringValue(text_readout.first);
    (*stat_obj_fields)["value"] = ValueUtil::stringValue(text_readout.second);
    stats_array.push_back(ValueUtil::structValue(stat_obj));
  }
  for (const auto& stat : all_stats_) {
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
  for (const Stats::ParentHistogramSharedPtr& histogram : histograms_) {
    if (shouldShowMetric(*histogram)) {
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

  response.add(MessageUtil::getJsonStringFromMessageOrDie(document, pretty_print, true));
#endif
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
