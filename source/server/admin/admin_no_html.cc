#include "source/server/admin/admin.h"

namespace Envoy {
namespace Server {

Http::Code AdminImpl::handlerAdminHome(Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Text);
  response.add("HTML output was disabled by building with --define=admin_html=disabled");
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
