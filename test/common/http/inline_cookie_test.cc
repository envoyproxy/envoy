#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {
namespace {

// Test that the cookie header can work correctly after being registered as an inline header. The
// test will register the cookie as an inline header. In order to avoid affecting other tests, the
// test is placed in this separate source file.
TEST(InlineCookieTest, InlineCookieTest) {
  Http::CustomInlineHeaderRegistry::registerInlineHeader<Http::RequestHeaderMap::header_map_type>(
      Http::Headers::get().Cookie);
  Http::CustomInlineHeaderRegistry::registerInlineHeader<Http::RequestHeaderMap::header_map_type>(
      Http::LowerCaseString("header_for_compare"));

  {
    Http::TestRequestHeaderMapImpl headers{{"cookie", "key1:value1"},
                                           {"cookie", "key2:value2"},
                                           {"header_for_compare", "value1"},
                                           {"header_for_compare", "value2"}};

    // Delimiter for inline 'cookie' header is specialized '; '.
    EXPECT_EQ("key1:value1; key2:value2", headers.get_("cookie"));
    // Delimiter for inline 'header_for_compare' header is default ','.
    EXPECT_EQ("value1,value2", headers.get_("header_for_compare"));
  }
}

} // namespace
} // namespace Http
} // namespace Envoy
