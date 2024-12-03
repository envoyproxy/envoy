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
    : StatsTextRender(params), active_(params.format_ == StatsFormat::ActiveHtml),
      json_histograms_(!active_ &&
                       params.histogram_buckets_mode_ == Utility::HistogramBucketsMode::Detailed) {
  AdminHtmlUtil::renderHead(response_headers, response);
}

void StatsHtmlRender::finalize(Buffer::Instance& response) {
  if (first_histogram_) {
    response.add("</pre>\n");
  }

  AdminHtmlUtil::finalize(response);
}

void StatsHtmlRender::setupStatsPage(const Admin::UrlHandler& url_handler,
                                     const StatsParams& params, Buffer::Instance& response) {
  AdminHtmlUtil::renderTableBegin(response);
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
  ASSERT(first_histogram_);
  response.addFragments({name, ": \"", Html::Utility::sanitize(value), "\"\n"});
}

void StatsHtmlRender::noStats(Buffer::Instance& response, absl::string_view types) {
  ASSERT(first_histogram_);
  if (!active_) {
    response.addFragments({"</pre>\n<br/><i>No ", types, " found</i><br/>\n<pre>\n"});
  }
}

// When using Detailed mode, we override the generate method for HTML to trigger
// some JS that will render the histogram graphically. We will render that from
// JavaScript and convey the histogram data to the JS via JSON, so we can
// delegate to an instantiated JSON `sub-renderer` that will write into
// json_data_. that `sub_renderer` will only be populated in Detailed mode.
//
// All other modes default to rendering the histogram textually.
void StatsHtmlRender::generate(Buffer::Instance& response, const std::string& name,
                               const Stats::ParentHistogram& histogram) {
  if (json_histograms_) {
    Json::BufferStreamer streamer(response);

    // If this is the first histogram we are rendering, then we need to first
    // generate the supported-percentiles array sand save it in a constant.
    //
    // We use a separate <script> tag for each histogram so that the browser can
    // begin parsing each potentially large histogram as it is generated,rather
    // than building up a huge json structure with all the histograms and
    // blocking rendering until that is parsed.
    if (first_histogram_) {
      first_histogram_ = false;
      response.add("</pre>\n<div id='histograms'></div>\n<script>\nconst supportedPercentiles = ");
      { StatsJsonRender::populateSupportedPercentiles(*streamer.makeRootArray()); }
      response.add(";\nconst histogramDiv = document.getElementById('histograms');\n");
      // The first histogram will share the first script tag with the histogram
      // div and supportedPercentiles array constants.
    } else {
      response.add("<script>\n");
    }
    response.add("renderHistogram(histogramDiv, supportedPercentiles,\n");
    { StatsJsonRender::generateHistogramDetail(name, histogram, *streamer.makeRootMap()); }
    response.add(");\n</script>\n");
  } else {
    StatsTextRender::generate(response, name, histogram);
  }
}

} // namespace Server
} // namespace Envoy
