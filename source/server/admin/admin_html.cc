#include "source/common/html/utility.h"
#include "source/server/admin/admin.h"
#include "source/server/admin/stats_html_render.h"

namespace Envoy {
namespace Server {

Http::Code AdminImpl::handlerAdminHome(absl::string_view, Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  StatsHtmlRender html(response_headers, response, StatsParams());
  html.renderTableBegin();

  // Prefix order is used during searching, but for printing do them in alpha order.
  OptRef<const Http::Utility::QueryParams> no_query_params;
  for (const UrlHandler* handler : sortedHandlers()) {
    html.renderUrlHandler(*handler, no_query_params);
  }

  html.renderTableEnd();

  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
