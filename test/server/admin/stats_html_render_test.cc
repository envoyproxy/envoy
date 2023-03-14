#include "source/server/admin/admin.h"
#include "source/server/admin/stats_html_render.h"

#include "test/server/admin/stats_render_test_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Not;

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
  constexpr absl::string_view expected =
      "h1: P0(200,200) P25(207.5,207.5) P50(302.5,302.5) P75(306.25,306.25) "
      "P90(308.5,308.5) P95(309.25,309.25) P99(309.85,309.85) P99.5(309.925,309.925) "
      "P99.9(309.985,309.985) P100(310,310)\n";
  EXPECT_THAT(render<>(renderer_, "h1", populateHistogram("h1", {200, 300, 300})),
              HasSubstr(expected));
}

TEST_F(StatsHtmlRenderTest, RenderHead) {
  // The "<head>...</head>" is rendered by the constructor.
  EXPECT_THAT(response_.toString(), HasSubstr("<head>"));
  EXPECT_THAT(response_.toString(), HasSubstr("</head>"));
}

TEST_F(StatsHtmlRenderTest, RenderTableBegin) {
  renderer_.tableBegin(response_);
  EXPECT_THAT(response_.toString(), HasSubstr("<table class='home-table'>"));
}

TEST_F(StatsHtmlRenderTest, RenderTableEnd) {
  renderer_.tableEnd(response_);
  EXPECT_THAT(response_.toString(), HasSubstr("</table>"));
}

TEST_F(StatsHtmlRenderTest, RenderUrlHandlerNoQuery) {
  renderer_.urlHandler(response_, handler(), query_params_);
  std::string out = response_.toString();
  EXPECT_THAT(out, HasSubstr("<input type='checkbox' name='boolparam' "
                             "id='param-1-prefix-boolparam' form='prefix'"));
  EXPECT_THAT(out, Not(HasSubstr(" checked/>")));
  EXPECT_THAT(out, HasSubstr("help text"));
  EXPECT_THAT(out, HasSubstr("param help"));
  EXPECT_THAT(out, HasSubstr("<button class='button-as-link'>prefix</button>"));
  EXPECT_THAT(out, Not(HasSubstr(" onchange='prefix.submit()")));
  EXPECT_THAT(out, Not(HasSubstr(" type='hidden' ")));
}

TEST_F(StatsHtmlRenderTest, RenderUrlHandlerWithQuery) {
  query_params_["boolparam"] = "on";
  query_params_["strparam"] = "str value";
  renderer_.urlHandler(response_, handler(), query_params_);
  std::string out = response_.toString();
  EXPECT_THAT(out,
              HasSubstr("<input type='checkbox' name='boolparam' id='param-1-prefix-boolparam' "
                        "form='prefix'"));
  EXPECT_THAT(out, HasSubstr("<input type='text' name='strparam' id='param-1-prefix-strparam' "
                             "form='prefix' value='str value'"));
  EXPECT_THAT(out, HasSubstr(" checked/>"));
  EXPECT_THAT(out, HasSubstr("help text"));
  EXPECT_THAT(out, HasSubstr("param help"));
  EXPECT_THAT(out, HasSubstr("<button class='button-as-link'>prefix</button>"));
  EXPECT_THAT(out, Not(HasSubstr(" onchange='prefix.submit()")));
  EXPECT_THAT(out, Not(HasSubstr(" type='hidden' ")));
}

TEST_F(StatsHtmlRenderTest, RenderUrlHandlerSubmitOnChange) {
  renderer_.setSubmitOnChange(true);
  renderer_.urlHandler(response_, handler(), query_params_);
  std::string out = response_.toString();
  EXPECT_THAT(out, HasSubstr(" onchange='prefix.submit()"));
  EXPECT_THAT(out, Not(HasSubstr("<button class='button-as-link'>prefix</button>")));
  EXPECT_THAT(out, Not(HasSubstr(" type='hidden' ")));
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
