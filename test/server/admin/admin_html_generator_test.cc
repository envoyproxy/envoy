#include "source/server/admin/admin_html_generator.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace Server {

class AdminHtmlGeneratorTest : public testing::Test {
protected:
  AdminHtmlGeneratorTest()
      : generator_(data_), handler_{
                               "/prefix",
                               "help text",
                               handlerCallback,
                               false,
                               false,
                               {{Admin::ParamDescriptor::Type::Boolean, "param", "param help"}}} {}

  static Http::Code handlerCallback(absl::string_view, Http::ResponseHeaderMap&, Buffer::Instance&,
                                    AdminStream&) {
    return Http::Code::OK;
  }

  Buffer::OwnedImpl data_;
  AdminHtmlGenerator generator_;
  Admin::UrlHandler handler_;
  Http::Utility::QueryParams query_params_;
};

TEST_F(AdminHtmlGeneratorTest, RenderHead) {
  generator_.renderHead();
  EXPECT_THAT(data_.toString(), HasSubstr("<head>"));
}

TEST_F(AdminHtmlGeneratorTest, RenderTableBegin) {
  generator_.renderTableBegin();
  EXPECT_THAT(data_.toString(), HasSubstr("<table class='home-table'>"));
}

TEST_F(AdminHtmlGeneratorTest, RenderTableEnd) {
  generator_.renderTableEnd();
  EXPECT_THAT(data_.toString(), HasSubstr("</table>"));
}

TEST_F(AdminHtmlGeneratorTest, RenderUrlHandlerNoQuery) {
  generator_.renderUrlHandler(handler_, query_params_);
  std::string out = data_.toString();
  EXPECT_THAT(out, HasSubstr("<input type='checkbox' name='param' id='param' form='prefix'"));
  EXPECT_THAT(out, Not(HasSubstr(" checked/>")));
  EXPECT_THAT(out, HasSubstr("help text"));
  EXPECT_THAT(out, HasSubstr("param help"));
  EXPECT_THAT(out, HasSubstr("<button class='button-as-link'>prefix</button>"));
  EXPECT_THAT(out, Not(HasSubstr(" onchange='prefix.submit()")));
}

TEST_F(AdminHtmlGeneratorTest, RenderUrlHandlerWithQuery) {
  query_params_["param"] = "on";
  generator_.renderUrlHandler(handler_, query_params_);
  std::string out = data_.toString();
  EXPECT_THAT(out, HasSubstr("<input type='checkbox' name='param' id='param' form='prefix'"));
  EXPECT_THAT(out, HasSubstr(" checked/>"));
  EXPECT_THAT(out, HasSubstr("help text"));
  EXPECT_THAT(out, HasSubstr("param help"));
  EXPECT_THAT(out, HasSubstr("<button class='button-as-link'>prefix</button>"));
  EXPECT_THAT(out, Not(HasSubstr(" onchange='prefix.submit()")));
}

TEST_F(AdminHtmlGeneratorTest, RenderUrlHandlerSubmitOnChange) {
  generator_.setSubmitOnChange(true);
  generator_.renderUrlHandler(handler_, query_params_);
  std::string out = data_.toString();
  EXPECT_THAT(out, HasSubstr(" onchange='prefix.submit()"));
  EXPECT_THAT(out, Not(HasSubstr("<button class='button-as-link'>prefix</button>")));
}

} // namespace Server
} // namespace Envoy
