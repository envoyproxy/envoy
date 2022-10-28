#include "source/common/html/utility.h"
#include "source/server/admin/admin.h"
#include "source/server/admin/stats_html_render.h"

namespace Envoy {
namespace Server {

Http::Code AdminImpl::handlerAdminHome(Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  StatsHtmlRender html(response_headers, response, StatsParams());
  html.tableBegin(response);

  // Prefix order is used during searching, but for printing do them in alpha order.
  OptRef<const Http::Utility::QueryParams> no_query_params;
  for (const UrlHandler* handler : sortedHandlers()) {
    html.urlHandler(response, *handler, no_query_params);
  }

  html.tableEnd(response);
  html.finalize(response);

  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
