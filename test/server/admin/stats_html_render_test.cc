#include "source/server/admin/admin.h"
#include "source/server/admin/stats_html_render.h"

#include "test/server/admin/stats_render_test_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

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
  StatsHtmlRender renderer_{response_headers_, response_, params_};
  Http::Utility::QueryParams query_params_;
};

TEST_F(StatsHtmlRenderTest, String) {
  EXPECT_THAT(render<std::string>(renderer_, "name", "abc 123 ~!@#$%^&*()-_=+;:'\",<.>/?"),
              HasSubstr("name: \"abc 123 ~!@#$%^&amp;*()-_=+;:&#39;&quot;,&lt;.&gt;/?\"\n"));
}

TEST_F(StatsHtmlRenderTest, HistogramNoBuckets) {
  // The goal of this test is to show that we have embedded the histogram as a json fragment.
  constexpr absl::string_view expected = "const json = \n{\"stats\":[{\"histograms\":";
  EXPECT_THAT(render<>(renderer_, "h1", populateHistogram("h1", {200, 300, 300})),
              HasSubstr(expected));
}
class StatsActiveRenderTest : public StatsRenderTestBase {
protected:
  StatsActiveRenderTest() {
    params_.format_ = StatsFormat::ActiveHtml;
    renderer_ = std::make_unique<StatsHtmlRender>(response_headers_, response_, params_);
  }

  std::unique_ptr<StatsHtmlRender> renderer_;
};

TEST_F(StatsActiveRenderTest, RenderActive) {
  renderer_->setupStatsPage(handler(), params_, response_);
  EXPECT_THAT(response_.toString(), HasSubstr("<script>"));
}

} // namespace Server
} // namespace Envoy
