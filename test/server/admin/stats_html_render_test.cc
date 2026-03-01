#include "source/server/admin/admin.h"
#include "source/server/admin/stats_html_render.h"

#include "test/server/admin/stats_render_test_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::EndsWith;
using testing::HasSubstr;
using testing::StartsWith;

namespace Envoy {
namespace Server {

static Admin::RequestPtr handlerCallback(AdminStream&) {
  return Admin::makeStaticTextRequest("", Http::Code::OK);
}

const Admin::UrlHandler handler() {
  return Admin::UrlHandler{"/prefix",
                           "help text",
                           handlerCallback,
                           false,
                           false,
                           {{Admin::ParamDescriptor::Type::Boolean, "boolparam", "bool param help"},
                            {Admin::ParamDescriptor::Type::String, "strparam", "str param help"}}};
}

class StatsHtmlRenderTest : public StatsRenderTestBase {
protected:
  Http::Utility::QueryParamsMulti query_params_;
};

TEST_F(StatsHtmlRenderTest, String) {
  StatsHtmlRender renderer{response_headers_, response_, params_};
  EXPECT_THAT(render<std::string>(renderer, "name", "abc 123 ~!@#$%^&*()-_=+;:'\",<.>/?"),
              HasSubstr("name: \"abc 123 ~!@#$%^&amp;*()-_=+;:&#39;&quot;,&lt;.&gt;/?\"\n"));
}

TEST_F(StatsHtmlRenderTest, HistogramUnset) {
  constexpr absl::string_view expected =
      "h1: P0(205,205) P25(205,205) P50(303.3333333333333,303.3333333333333) "
      "P75(306.6666666666667,306.6666666666667) "
      "P90(306.6666666666667,306.6666666666667) P95(306.6666666666667,306.6666666666667) "
      "P99(306.6666666666667,306.6666666666667) P99.5(306.6666666666667,306.6666666666667) "
      "P99.9(306.6666666666667,306.6666666666667) P100(306.6666666666667,306.6666666666667)\n";
  StatsHtmlRender renderer{response_headers_, response_, params_};
  EXPECT_THAT(render<>(renderer, "h1", populateHistogram("h1", {200, 300, 300})),
              HasSubstr(expected));
}

TEST_F(StatsHtmlRenderTest, HistogramSummary) {
  constexpr absl::string_view expected =
      "h1: P0(205,205) P25(205,205) P50(303.3333333333333,303.3333333333333) "
      "P75(306.6666666666667,306.6666666666667) "
      "P90(306.6666666666667,306.6666666666667) P95(306.6666666666667,306.6666666666667) "
      "P99(306.6666666666667,306.6666666666667) P99.5(306.6666666666667,306.6666666666667) "
      "P99.9(306.6666666666667,306.6666666666667) P100(306.6666666666667,306.6666666666667)\n";
  params_.histogram_buckets_mode_ = Utility::HistogramBucketsMode::Summary;
  StatsHtmlRender renderer{response_headers_, response_, params_};
  EXPECT_THAT(render<>(renderer, "h1", populateHistogram("h1", {200, 300, 300})),
              HasSubstr(expected));
}

TEST_F(StatsHtmlRenderTest, HistogramStreamingDetailed) {
  // The goal of this test is to show that we have embedded the histogram as a
  // json fragment, which gets streamed out to HTML one histogram at a time,
  // rather than buffered.
  params_.histogram_buckets_mode_ = Utility::HistogramBucketsMode::Detailed;

  StatsHtmlRender renderer{response_headers_, response_, params_};
  renderer.generate(response_, "scalar", 42);
  EXPECT_THAT(response_.toString(), StartsWith("<!DOCTYPE html>\n<html lang='en'>\n<head>\n"));
  EXPECT_THAT(response_.toString(), EndsWith("\nscalar: 42\n"));
  response_.drain(response_.length());

  renderer.generate(response_, "h1", populateHistogram("h1", {200, 300, 300}));
  EXPECT_THAT(response_.toString(), StartsWith("</pre>\n<div id='histograms'></div>\n"
                                               "<script>\nconst supportedPercentiles = ["));
  EXPECT_THAT(response_.toString(),
              HasSubstr("\nconst histogramDiv = document.getElementById('histograms');\n"
                        "renderHistogram(histogramDiv, supportedPercentiles,\n"
                        "{\"name\":\"h1\",\"totals\":[{\"lower_bound\":"));
  EXPECT_THAT(response_.toString(), EndsWith("\n</script>\n"));
  response_.drain(response_.length());

  renderer.generate(response_, "h2", populateHistogram("h2", {200, 300, 300}));
  EXPECT_THAT(response_.toString(),
              StartsWith("<script>\nrenderHistogram(histogramDiv, supportedPercentiles,\n"
                         "{\"name\":\"h2\",\"totals\":[{\"lower_bound\":"));
  EXPECT_THAT(response_.toString(), EndsWith("\n</script>\n"));
  response_.drain(response_.length());

  renderer.finalize(response_);
  EXPECT_EQ("</body>\n</html>\n", response_.toString());
}

TEST_F(StatsHtmlRenderTest, RenderActive) {
  params_.format_ = StatsFormat::ActiveHtml;
  StatsHtmlRender renderer(response_headers_, response_, params_);
  renderer.setupStatsPage(handler(), params_, response_);
  EXPECT_THAT(response_.toString(), HasSubstr("<script>"));
}

} // namespace Server
} // namespace Envoy
