#include <memory>

#include "common/matcher/matcher.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(Matcher, SimpleMatcher) {
  auto http_mapper = std::make_unique<HttpKeyNamespaceMapper>();
  auto no_match_tree = std::make_unique<AlwaysCallbackMatcher>("no_match");
  MultimapMatcher matcher("foo", "request_headers", std::move(http_mapper),
                          std::move(no_match_tree));

  HttpMatchingData http_data;
  auto headers = Http::TestRequestHeaderMapImpl({{"foo", "bar"}});
  http_data.request_headers_ = &headers;
  EXPECT_EQ(matcher.match(http_data)->callback().value(), "no_match");
}

} // namespace Envoy