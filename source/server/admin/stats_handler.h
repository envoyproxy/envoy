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
  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, AdminStream&);
  Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response);
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

  Admin::RequestPtr makeRequest(absl::string_view path, AdminStream& admin_stream);
  static Admin::RequestPtr makeRequest(Stats::Store& stats, const StatsParams& params,
                                       StatsRequest::UrlHandlerFn url_handler_fn);
private:
  class Context;
  class HtmlRender;
  class JsonRender;
  class Render;
  class TextRender;

  friend class StatsHandlerTest;

  static Http::Code prometheusStats(absl::string_view path_and_query, Buffer::Instance& response,
                                    Stats::Store& stats,
                                    Stats::CustomStatNamespaces& custom_namespaces);
};

} // namespace Server
} // namespace Envoy
