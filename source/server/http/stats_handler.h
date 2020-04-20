#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "common/stats/histogram_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class StatsHandler {

public:
  static Http::Code handlerResetCounters(absl::string_view path_and_query,
                                         Http::ResponseHeaderMap& response_headers,
                                         Buffer::Instance& response, AdminStream&,
                                         Server::Instance& server);
  static Http::Code handlerStatsRecentLookups(absl::string_view path_and_query,
                                              Http::ResponseHeaderMap& response_headers,
                                              Buffer::Instance& response, AdminStream&,
                                              Server::Instance& server);
  static Http::Code handlerStatsRecentLookupsClear(absl::string_view path_and_query,
                                                   Http::ResponseHeaderMap& response_headers,
                                                   Buffer::Instance& response, AdminStream&,
                                                   Server::Instance& server);
  static Http::Code handlerStatsRecentLookupsDisable(absl::string_view path_and_query,
                                                     Http::ResponseHeaderMap& response_headers,
                                                     Buffer::Instance& response, AdminStream&,
                                                     Server::Instance& server);
  static Http::Code handlerStatsRecentLookupsEnable(absl::string_view path_and_query,
                                                    Http::ResponseHeaderMap& response_headers,
                                                    Buffer::Instance& response, AdminStream&,
                                                    Server::Instance& server);
  static Http::Code handlerStats(absl::string_view path_and_query,
                                 Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, AdminStream&,
                                 Server::Instance& server);
  static Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                           Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response, AdminStream&,
                                           Server::Instance& server);

private:
  template <class StatType>
  static bool shouldShowMetric(const StatType& metric, const bool used_only,
                               const absl::optional<std::regex>& regex) {
    return ((!used_only || metric.used()) &&
            (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
  }

  friend class AdminStatsTest;

  static std::string statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                                 const std::map<std::string, std::string>& text_readouts,
                                 const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                                 bool used_only,
                                 const absl::optional<std::regex> regex = absl::nullopt,
                                 bool pretty_print = false);
};

/**
 * Formatter for metric/labels exported to Prometheus.
 *
 * See: https://prometheus.io/docs/concepts/data_model
 */
class PrometheusStatsFormatter {
public:
  /**
   * Extracts counters and gauges and relevant tags, appending them to
   * the response buffer after sanitizing the metric / label names.
   * @return uint64_t total number of metric types inserted in response.
   */
  static uint64_t statsAsPrometheus(const std::vector<Stats::CounterSharedPtr>& counters,
                                    const std::vector<Stats::GaugeSharedPtr>& gauges,
                                    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                                    Buffer::Instance& response, const bool used_only,
                                    const absl::optional<std::regex>& regex);
  /**
   * Format the given tags, returning a string as a comma-separated list
   * of <tag_name>="<tag_value>" pairs.
   */
  static std::string formattedTags(const std::vector<Stats::Tag>& tags);
  /**
   * Format the given metric name, prefixed with "envoy_".
   */
  static std::string metricName(const std::string& extracted_name);

private:
  /**
   * Take a string and sanitize it according to Prometheus conventions.
   */
  static std::string sanitizeName(const std::string& name);

  /*
   * Determine whether a metric has never been emitted and choose to
   * not show it if we only wanted used metrics.
   */
  template <class StatType>
  static bool shouldShowMetric(const StatType& metric, const bool used_only,
                               const absl::optional<std::regex>& regex) {
    return ((!used_only || metric.used()) &&
            (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
  }
};

} // namespace Server
} // namespace Envoy
