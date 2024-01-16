#include "source/common/html/utility.h"
#include "source/common/http/headers.h"
#include "source/server/admin/admin.h"
#include "source/server/admin/admin_html_util.h"
#include "source/server/admin/html/admin_html_gen.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Server {

Http::Code AdminImpl::handlerAdminHome(Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  AdminHtmlUtil::renderHead(response_headers, response);
  AdminHtmlUtil::renderTableBegin(response);

  // Prefix order is used during searching, but for printing do them in alpha order.
  OptRef<const Http::Utility::QueryParamsMulti> no_query_params;
  uint32_t index = 0;
  for (const UrlHandler* handler : sortedHandlers()) {
    AdminHtmlUtil::renderEndpointTableRow(response, *handler, no_query_params, ++index, false,
                                          false);
  }

  AdminHtmlUtil::renderTableEnd(response);
  AdminHtmlUtil::finalize(response);

  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
