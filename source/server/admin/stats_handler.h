#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/common/stats/histogram_impl.h"
#include "source/server/admin/handler_ctx.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class StatsHandler : public HandlerContextBase {

public:
  enum class Format {
    Html,
    Json,
    Prometheus,
    Text,
  };

  // The order is used to linearize the ordering of stats of all types.
  enum class Type {
    TextReadouts,
    Counters,
    Gauges,
    Histograms,
    All,
  };

  struct Params {
    Http::Code parse(absl::string_view url, Buffer::Instance& response);
    bool shouldShowMetric(const Stats::Metric& metric) const;
    bool canRejectStats() const { return filter_.has_value() || used_only_; }

    bool used_only_{false};
    bool prometheus_text_readouts_{false};
    bool pretty_{false};
    Format format_{
        Format::Text}; // If no `format=` param we use Text, but the `UI` defaults to HTML.
    Type type_{Type::All};
    Type start_type_{Type::TextReadouts};
    std::string filter_string_;
    absl::optional<std::regex> filter_;
    Http::Utility::QueryParams query_;
  };

  StatsHandler(Server::Instance& server);

  Http::Code handlerResetCounters(absl::string_view path_and_query,
                                  Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookups(absl::string_view path_and_query,
                                       Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsClear(absl::string_view path_and_query,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsDisable(absl::string_view path_and_query,
                                              Http::ResponseHeaderMap& response_headers,
                                              Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsEnable(absl::string_view path_and_query,
                                             Http::ResponseHeaderMap& response_headers,
                                             Buffer::Instance& response, AdminStream&);
  Http::Code handlerStats(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                          AdminStream&);
  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);
  Http::Code handlerContention(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

  /**
   * When stats are rendered in HTML mode, we want users to be able to tweak
   * parameters after the stats page is rendered, such as tweaking the filter or
   * `usedonly`. We use the same stats UrlHandler both for the admin home page
   * and for rendering in /stats?format=html. We share the same UrlHandler in
   * both contexts by defining an API for it here.
   *
   * @return a URL handler for stats.
   */
  Admin::UrlHandler statsHandler();

  /**
   * @return a string representation for a type.
   */
  static absl::string_view typeToString(Type type);

  class Render;
  class Context {
   public:
#define STRING_SORT 0
#if STRING_SORT
    template <class ValueType> using NameValue = std::pair<std::string, ValueType>;
    template <class ValueType> using NameValueVec = std::vector<NameValue<ValueType>>;
#else
    template <class StatType> using StatVec = std::vector<Stats::RefcountPtr<StatType>>;
#endif

    Context(const Params& params, Stats::Store& store,
            Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);

    // Iterates through the various stat types, and renders them.
    void collectAndEmitStats(const Stats::Store& stats);
#if STRING_SORT
    template <class ValueType> void emit(NameValueVec<ValueType>& name_value_vec);
#else
    template <class StatType>
        void emit(StatVec<StatType>& stat_vec, const Stats::SymbolTable& symbol_table);
#endif

#if STRING_SORT
    template <class StatType, class ValueType>
        void collect(Type type, const Stats::Store& stats, NameValueVec<ValueType>& vec);
#else
    template <class StatType>
        void collect(Type type, const Stats::Store& stats, StatVec<StatType>& vec);
#endif

    std::string saveValue(Stats::TextReadout& text_readout) { return text_readout.value(); }
    uint64_t saveValue(Stats::Counter& counter) { return counter.value(); }
    uint64_t saveValue(Stats::Gauge& gauge) { return gauge.value(); }
    Stats::ParentHistogramSharedPtr saveValue(Stats::ParentHistogram& histogram) {
      return Stats::ParentHistogramSharedPtr(&histogram);
    }

    void collectHelper(const Stats::Store& stats, std::function<void(size_t)> size_fn,
                                              Stats::StatFn<Stats::TextReadout&> fn) {
      stats.forEachTextReadout(size_fn, fn);
    }

    void collectHelper(const Stats::Store& stats, std::function<void(size_t)> size_fn,
                                              Stats::StatFn<Stats::Counter&> fn) {
      stats.forEachCounter(size_fn, fn);
    }

    void collectHelper(const Stats::Store& stats, std::function<void(size_t)> size_fn,
                                              Stats::StatFn<Stats::Gauge&> fn) {
      stats.forEachGauge(size_fn, fn);
    }

    void collectHelper(const Stats::Store& stats, std::function<void(size_t)> size_fn,
                       Stats::StatFn<Stats::ParentHistogram> fn);

   private:
    void nextPhase() { phase_ = static_cast<Type>(static_cast<uint32_t>(phase_) + 1); }


    const Params& params_;
    std::unique_ptr<Render> render_;
    Stats::Store& store_;
    Stats::SymbolTable& symbol_table_;

#if !STRING_SORT
    Type phase_{Type::TextReadouts};
    StatVec<Stats::TextReadout> text_readouts_;
    StatVec<Stats::Counter> counters_;
    StatVec<Stats::Gauge> gauges_;
    StatVec<Stats::ParentHistogram> histograms_;
    uint32_t text_readout_index_{0};
    uint32_t counter_index_{0};
    uint32_t gauge_index_{0};
    uint32_t histogram_index_{0};
#endif
    const uint64_t mem_start_;
    uint64_t mem_max_{0};
  };
  using ContextPtr = std::unique_ptr<Context>;

private:
  class HtmlRender;
  class JsonRender;
  class TextRender;

  friend class StatsHandlerTest;

  static Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response,
                                    Stats::Store& stats,
                                    Stats::CustomStatNamespaces& custom_namespaces);

  absl::flat_hash_map<AdminStream*, ContextPtr> context_map_;
};

} // namespace Server
} // namespace Envoy
