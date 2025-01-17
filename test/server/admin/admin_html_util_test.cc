#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/admin_html_util.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::Not;

namespace Envoy {
namespace Server {

const Admin::UrlHandler handler() {
  return Admin::UrlHandler{"/prefix",
                           "help text",
                           nullptr,
                           false,
                           false,
                           {{Admin::ParamDescriptor::Type::Boolean, "boolparam", "bool param help"},
                            {Admin::ParamDescriptor::Type::String, "strparam", "str param help"}}};
}

class AdminHtmlUtilTest : public testing::Test {
protected:
  Buffer::OwnedImpl response_;
  Http::Utility::QueryParamsMulti query_params_;
};

TEST_F(AdminHtmlUtilTest, RenderTableBegin) {
  AdminHtmlUtil::renderTableBegin(response_);
  EXPECT_THAT(response_.toString(), HasSubstr("<table class='home-table'>"));
}

TEST_F(AdminHtmlUtilTest, RenderTableEnd) {
  AdminHtmlUtil::renderTableEnd(response_);
  EXPECT_THAT(response_.toString(), HasSubstr("</table>"));
}

TEST_F(AdminHtmlUtilTest, RenderRenderEndpointTableRowNoQuery) {
  AdminHtmlUtil::renderEndpointTableRow(response_, handler(), query_params_, 42, false, false);
  std::string out = response_.toString();
  EXPECT_THAT(out, HasSubstr("<input type='checkbox' name='boolparam' "
                             "id='param-42-prefix-boolparam' form='prefix'"));
  EXPECT_THAT(out, Not(HasSubstr(" checked/>")));
  EXPECT_THAT(out, HasSubstr("help text"));
  EXPECT_THAT(out, HasSubstr("param help"));
  EXPECT_THAT(out, HasSubstr("<button class='button-as-link'>prefix</button>"));
  EXPECT_THAT(out, Not(HasSubstr(" onchange='prefix.submit()")));
  EXPECT_THAT(out, Not(HasSubstr(" type='hidden' ")));
}

TEST_F(AdminHtmlUtilTest, RenderRenderEndpointTableRowWithQuery) {
  query_params_.overwrite("boolparam", "on");
  query_params_.overwrite("strparam", "str value");
  AdminHtmlUtil::renderEndpointTableRow(response_, handler(), query_params_, 31415, false, false);
  std::string out = response_.toString();
  EXPECT_THAT(out,
              HasSubstr("<input type='checkbox' name='boolparam' id='param-31415-prefix-boolparam' "
                        "form='prefix'"));
  EXPECT_THAT(out, HasSubstr("<input type='text' name='strparam' id='param-31415-prefix-strparam' "
                             "form='prefix' value='str value'"));
  EXPECT_THAT(out, HasSubstr(" checked/>"));
  EXPECT_THAT(out, HasSubstr("help text"));
  EXPECT_THAT(out, HasSubstr("param help"));
  EXPECT_THAT(out, HasSubstr("<button class='button-as-link'>prefix</button>"));
  EXPECT_THAT(out, Not(HasSubstr(" onchange='prefix.submit()")));
  EXPECT_THAT(out, Not(HasSubstr(" type='hidden' ")));
}

TEST_F(AdminHtmlUtilTest, RenderHead) {
  // The "<head>...</head>" is rendered by the constructor.
  Http::TestResponseHeaderMapImpl response_headers;
  AdminHtmlUtil::renderHead(response_headers, response_);
  EXPECT_THAT(response_.toString(), HasSubstr("<head>"));
  EXPECT_THAT(response_.toString(), HasSubstr("</head>"));
  EXPECT_EQ(Http::Headers::get().ContentTypeValues.Html,
            response_headers.ContentType()->value().getStringView());
}

TEST_F(AdminHtmlUtilTest, RenderRenderEndpointTableRowSubmitOnChange) {
  AdminHtmlUtil::renderEndpointTableRow(response_, handler(), query_params_, 0, true, false);
  std::string out = response_.toString();
  EXPECT_THAT(out, HasSubstr(" onchange='prefix.submit()"));
  EXPECT_THAT(out, Not(HasSubstr("<button class='button-as-link'>prefix</button>")));
  EXPECT_THAT(out, Not(HasSubstr(" type='hidden' ")));
}

namespace {
class TestResourceProvider : public Server::AdminHtmlUtil::ResourceProvider {
public:
  static constexpr absl::string_view testResourceValue = "This is only a test";

  absl::string_view getResource(absl::string_view, std::string&) override {
    return testResourceValue;
  }
};
} // namespace

TEST_F(AdminHtmlUtilTest, OverrideResourceProvider) {
  std::unique_ptr<AdminHtmlUtil::ResourceProvider> prev =
      AdminHtmlUtil::setResourceProvider(std::make_unique<TestResourceProvider>());
  std::string buf;
  EXPECT_EQ(TestResourceProvider::testResourceValue, AdminHtmlUtil::getResource("foo", buf));
  AdminHtmlUtil::setResourceProvider(std::move(prev));
}

} // namespace Server
} // namespace Envoy
