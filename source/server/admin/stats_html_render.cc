#include "source/server/admin/stats_html_render.h"

#include "source/common/html/utility.h"

namespace Envoy {
namespace Server {

StatsHtmlRender::StatsHtmlRender(Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, const Admin::UrlHandler& url_handler,
                                 const StatsParams& params)
    : StatsTextRender(params), html_(response) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  html_.setSubmitOnChange(true);
  html_.renderHead();
  response.add("<body>\n");
  html_.renderTableBegin();
  html_.renderUrlHandler(url_handler, params.query_);
  html_.renderTableEnd();
  response.add("<pre>\n");
}

void StatsHtmlRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  response.addFragments({name, ": \"", Html::Utility::sanitize(value), "\"\n"});
}

void StatsHtmlRender::finalize(Buffer::Instance& response) { response.add("</pre></body>\n"); }

void StatsHtmlRender::noStats(Buffer::Instance& response, absl::string_view types) {
  response.addFragments({"</pre>\n<br/><i>No ", types, " found</i><br/>\n<pre>\n"});
}

} // namespace Server
} // namespace Envoy
