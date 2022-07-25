#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"
#include "source/server/admin/stats_request.h"
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
<<<<<<< HEAD
  Http::Code handlerStats(absl::string_view path_and_query,
                          Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                          AdminStream&);
  Http::Code handlerStatsPrometheus(absl::string_view path_and_query,
=======
  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
>>>>>>> admin-params
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);

  /**
   * Parses and executes a prometheus stats request.
   *
   * @param path_and_query the URL path and query
   * @param response buffer into which to write response
   * @return http response code
   */
  Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response);

  /**
   * Checks the server_ to see if a flush is needed, and then renders the
   * prometheus stats request.
   *
   * @params params the already-parsed parameters.
   * @param response buffer into which to write response
   */
  void prometheusFlushAndRender(const StatsParams& params, Buffer::Instance& response);

  /**
   * Renders the stats as prometheus. This is broken out as a separately
   * callable API to facilitate the benchmark
   * (test/server/admin/stats_handler_speed_test.cc) which does not have a
   * server object.
   *
   * @params stats the stats store to read
   * @param custom_namespaces namespace mappings used for prometheus
   * @params params the already-parsed parameters.
   * @param response buffer into which to write response
   */
  static void prometheusRender(Stats::Store& stats,
                               const Stats::CustomStatNamespaces& custom_namespaces,
                               const StatsParams& params, Buffer::Instance& response);

  Http::Code handlerContention(absl::string_view path_and_query,
                               Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);

<<<<<<< HEAD
  Admin::UrlHandler statsHandler();

private:
  class Context;
  class HtmlRender;
  class JsonRender;
  class Render;
  class TextRender;

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
    bool matchesAny() const;

    bool used_only_{false};
    bool prometheus_text_readouts_{false};
    bool pretty_{false};
    Format format_{
        Format::Text}; // If no `format=` param we use Text, but the `UI` defaults to HTML.
    Type type_{Type::All};
    Type start_type_{Type::TextReadouts};
    std::string filter_string_;
    absl::optional<std::regex> filter_;
    std::string scope_;
    Http::Utility::QueryParams query_;
  };

  friend class StatsHandlerTest;

  Http::Code stats(const Params& parmams, Stats::Store& store,
                   Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);

  static Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response,
                                    Stats::Store& stats,
                                    Stats::CustomStatNamespaces& custom_namespaces);

  static std::string statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                                 const std::map<std::string, std::string>& text_readouts,
                                 const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                                 bool pretty_print);

  static absl::string_view typeToString(Type type);
=======
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

  static Admin::RequestPtr makeRequest(Stats::Store& stats, const StatsParams& params,
                                       StatsRequest::UrlHandlerFn url_handler_fn = nullptr);
  Admin::RequestPtr makeRequest(absl::string_view path, AdminStream&);
  // static Admin::RequestPtr makeRequest(Stats::Store& stats, const StatsParams& params,
  //                                     StatsRequest::UrlHandlerFn url_handler_fn);

private:
  static Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response,
                                    Stats::Store& stats,
                                    Stats::CustomStatNamespaces& custom_namespaces);
>>>>>>> admin-params
};

} // namespace Server
} // namespace Envoy
