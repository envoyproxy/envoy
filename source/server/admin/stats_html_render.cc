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
    : StatsTextRender(params), active_(params.active_html_) {
  AdminHtmlUtil::renderHead(response_headers, response);
  if (!active_) {
    StatsParams json_params(params);
    json_params.histogram_buckets_mode_ = Utility::HistogramBucketsMode::Detailed;
    json_response_headers_ = Http::ResponseHeaderMapImpl::create();
    histogram_json_render_ =
        std::make_unique<StatsJsonRender>(*json_response_headers_, json_data_, json_params);
  }
}

void StatsHtmlRender::finalize(Buffer::Instance& response) {
  // Render all the histograms here using the JSON data we've accumulated
  // for them.
  if (!active_) {
    histogram_json_render_->finalize(json_data_);
    response.add("</pre>\n<div id='histograms'></div>\n<script>\nconst json = \n");
    response.add(json_data_);
    response.add(";\nrenderHistograms(document.getElementById('histograms'), json);\n</script\n");
  }

  AdminHtmlUtil::finalize(response);
}

void StatsHtmlRender::setupStatsPage(const Admin::UrlHandler& url_handler,
                                     const StatsParams& params, Buffer::Instance& response) {
  AdminHtmlUtil::renderTableBegin(response, "Parameter");
  AdminHtmlUtil::renderEndpointTableRow(response, url_handler, params.query_, 1, !active_, active_);
  if (active_) {
    std::string buf;
    response.add(AdminHtmlUtil::getResource("active_params.html", buf));
  }
  AdminHtmlUtil::renderTableEnd(response);
  std::string buf;
  if (active_) {
    std::string buf2;
    response.addFragments({"<script>\n", AdminHtmlUtil::getResource("histograms.js", buf),
                           AdminHtmlUtil::getResource("active_stats.js", buf2), "</script>\n"});
  } else {
    response.addFragments(
        {"<script>\n", AdminHtmlUtil::getResource("histograms.js", buf), "</script>\n<pre>\n"});
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

#if 0
void StatsHtmlRender::generate(Buffer::Instance& response, const std::string& name,
                               const Stats::ParentHistogram& histogram) {
  uint32_t index = ++histogram_index_;
  response.addFragments({"<div id='"histogram-, index,
          "'></div><script>renderHistogram('histogram-'", index, "', '",

><div class='histogram-name'>", Html::Utility::sanitize(name),
          "</div>

  // Note: we ignore buckets_mode in HTML and always renter a detailed view.
  switch (histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::NoBuckets:
    response.addFragments({name, ": ", histogram.quantileSummary(), "\n"});
    break;
  case Utility::HistogramBucketsMode::Cumulative:
    response.addFragments({name, ": ", histogram.bucketSummary(), "\n"});
    break;
  case Utility::HistogramBucketsMode::Disjoint:
    addDisjointBuckets(name, histogram, response);
    break;
  case Utility::HistogramBucketsMode::Detailed:
    //addDetailedBuckets(name, histogram, response);
    break;
  }
}
#endif

} // namespace Server
} // namespace Envoy
