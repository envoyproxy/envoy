#include <string>

#include "common/http/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

TEST(HttpUtility, parseQueryString) {
  Utility::QueryParamsImpl query_params_impl;
  EXPECT_EQ(Utility::QueryParamsMap(), query_params_impl.parseQueryString("/hello"));
  EXPECT_EQ(Utility::QueryParamsMap(), query_params_impl.parseQueryString("/hello?"));
  EXPECT_EQ(Utility::QueryParamsMap({{"hello", ""}}),
            query_params_impl.parseQueryString("/hello?hello"));
  EXPECT_EQ(Utility::QueryParamsMap({{"hello", "world"}}),
            query_params_impl.parseQueryString("/hello?hello=world"));
  EXPECT_EQ(Utility::QueryParamsMap({{"hello", ""}}),
            query_params_impl.parseQueryString("/hello?hello="));
  EXPECT_EQ(Utility::QueryParamsMap({{"hello", ""}}),
            query_params_impl.parseQueryString("/hello?hello=&"));
  EXPECT_EQ(Utility::QueryParamsMap({{"hello", ""}, {"hello2", "world2"}}),
            query_params_impl.parseQueryString("/hello?hello=&hello2=world2"));
  EXPECT_EQ(Utility::QueryParamsMap({{"name", "admin"}, {"level", "trace"}}),
            query_params_impl.parseQueryString("/logging?name=admin&level=trace"));
}

TEST(HttpUtility, QueryParamsToString) {
  Utility::QueryParamsImpl query_params_impl;
  EXPECT_EQ("", query_params_impl.queryParamsToString(Utility::QueryParamsMap({})));
  EXPECT_EQ("?a=1", query_params_impl.queryParamsToString(Utility::QueryParamsMap({{"a", "1"}})));
  EXPECT_EQ("?a=1&b=2", query_params_impl.queryParamsToString(
                            Utility::QueryParamsMap({{"a", "1"}, {"b", "2"}})));
}

} // namespace Http
} // namespace Envoy
