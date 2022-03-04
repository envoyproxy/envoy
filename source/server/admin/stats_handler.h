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
#include "source/server/admin/utils.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class StatsHandler : public HandlerContextBase {

public:
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
  static Http::Code handlerStats(Stats::Store& stats, bool used_only, bool json,
                                 const absl::optional<std::regex>& filter,
                                 Utility::HistogramBucketsMode histogram_buckets_mode,
                                 Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response);

  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);
  Http::Code handlerContention(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

private:
  template <class StatType>
  static bool shouldShowMetric(const StatType& metric, const bool used_only,
                               const absl::optional<std::regex>& regex) {
    return ((!used_only || metric.used()) &&
            (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
  }

  friend class StatsHandlerTest;

  static std::string statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                                 const std::map<std::string, std::string>& text_readouts,
                                 const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                                 bool used_only, const absl::optional<std::regex>& regex,
                                 Utility::HistogramBucketsMode histogram_buckets_mode,
                                 bool pretty_print = false);

  static void statsAsText(const std::map<std::string, uint64_t>& all_stats,
                          const std::map<std::string, std::string>& text_readouts,
                          const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                          bool used_only, const absl::optional<std::regex>& regex,
                          Utility::HistogramBucketsMode histogram_buckets_mode,
                          Buffer::Instance& response);

  static std::string computeDisjointBucketSummary(const Stats::ParentHistogramSharedPtr& histogram);

  static void statsAsJsonQuantileSummaryHelper(
      Protobuf::Map<std::string, ProtobufWkt::Value>& histograms_obj_container_fields,
      const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms, bool used_only,
      const absl::optional<std::regex>& regex);

  static void statsAsJsonHistogramBucketsHelper(
      Protobuf::Map<std::string, ProtobufWkt::Value>& histograms_obj_container_fields,
      const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms, bool used_only,
      const absl::optional<std::regex>& regex,
      std::function<std::vector<uint64_t>(const Stats::HistogramStatistics&)> computed_buckets);

  static ProtobufWkt::Value statsAsJsonHistogramBucketsCreateHistogramElementHelper(
      Stats::ConstSupportedBuckets& supported_buckets,
      const std::vector<uint64_t>& interval_buckets,
      const std::vector<uint64_t>& cumulative_buckets, const std::string& name);
};

} // namespace Server
} // namespace Envoy
