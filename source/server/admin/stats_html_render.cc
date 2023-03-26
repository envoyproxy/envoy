#include "source/server/admin/stats_html_render.h"

#include <algorithm>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/filesystem/filesystem_impl.h"
#include "source/common/html/utility.h"
#include "source/server/admin/admin_html_util.h"

#include "absl/strings/str_replace.h"

// Note: if you change this file, it's advisable to manually run
// test/integration/admin_html/web_test.sh to semi-automatically validate
// the web interface, in addition to updating and running unit tests.
//
// The admin web test does not yet run automatically.

namespace Envoy {
namespace Server {

StatsHtmlRender::StatsHtmlRender(Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, const StatsParams& params)
    : StatsTextRender(params), active_(params.format_ == StatsFormat::ActiveHtml) {
  AdminHtmlUtil::renderHead(response_headers, response);
}

void StatsHtmlRender::finalize(Buffer::Instance& response) { AdminHtmlUtil::finalize(response); }

void StatsHtmlRender::setupStatsPage(const Admin::UrlHandler& url_handler,
                                     const StatsParams& params, Buffer::Instance& response) {
  AdminHtmlUtil::renderTableBegin(response);
  AdminHtmlUtil::renderEndpointTableRow(response, url_handler, params.query_, 1, !active_, active_);
  if (active_) {
    std::string buf;
    response.add(AdminHtmlUtil::getResource("active_params.html", buf));
  }
  AdminHtmlUtil::renderTableEnd(response);
  if (active_) {
    std::string buf;
    response.addFragments(
        {"<script>\n", AdminHtmlUtil::getResource("active_stats.js", buf), "</script>\n"});
  } else {
    response.add("<pre>\n");
  }
}

void StatsHtmlRender::generate(Buffer::Instance& response, const std::string& name,
                               const std::string& value) {
  response.addFragments({name, ": \"", Html::Utility::sanitize(value), "\"\n"});
}

void StatsHtmlRender::noStats(Buffer::Instance& response, absl::string_view types) {
  if (!active_) {
    response.addFragments({"</pre>\n<br/><i>No ", types, " found</i><br/>\n<pre>\n"});
  }
}

} // namespace Server
} // namespace Envoy
