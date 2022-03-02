#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_functions.h"

#include "test/test_common/utility.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

using google::api::expr::runtime::CelValue;

// tests for the custom CEL functions in the reference implementation

class CustomCelFunctionTests : public testing::Test {
public:
  Protobuf::Arena arena;
  CelValue result;

  void urlFunctionTest(absl::flat_hash_map<std::string, std::string> map,
                       absl::StatusCode expected_status_code, absl::string_view expected_url) {
    UrlFunction function("url");
    CelValue cel_map = Utility::createCelMap(arena, map);
    std::vector<CelValue> input_values = {cel_map};
    auto args = absl::Span<CelValue>(input_values);
    absl::Status status = function.Evaluate(args, &result, &arena);
    ASSERT_EQ(status.code(), expected_status_code);
    if (status.ok()) {
      ASSERT_EQ(result.StringOrDie().value(), expected_url);
    } else {
      ASSERT_TRUE(result.IsError());
    }
  }
};

TEST_F(CustomCelFunctionTests, UrlFunctionTests) {
  std::vector<std::pair<CelValue, CelValue>> header_key_value_pairs;
  absl::flat_hash_map<std::string, std::string> map = {{"host", "abc.com:1234"}, {"path", ""}};

  urlFunctionTest(map, absl::StatusCode::kOk, "abc.com:1234");

  map.clear();
  map["host"] = "abc.com:1234";
  urlFunctionTest(map, absl::StatusCode::kNotFound, "");

  map.clear();
  map["path"] = "";
  urlFunctionTest(map, absl::StatusCode::kNotFound, "");
}

TEST_F(CustomCelFunctionTests, CookieTests) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {"cookie", "fruit=apple"}, {"cookie", "fruit=banana"}, {"cookie", "veg=carrot"}};
  auto cookie_map = cookie(&arena, request_headers).MapOrDie();
  auto value = (*cookie_map)[CelValue::CreateStringView("fruit")]->StringOrDie().value();
  ASSERT_EQ(value, "apple");
  value = (*cookie_map)[CelValue::CreateStringView("veg")]->StringOrDie().value();
  ASSERT_EQ(value, "carrot");

  Http::TestRequestHeaderMapImpl no_cookie_request_headers = {{"path", "/query?a=apple"}};
  auto error = cookie(&arena, no_cookie_request_headers);
  ASSERT_TRUE(error.IsError());
  ASSERT_EQ(error.ErrorOrDie()->message(), "cookie() no cookies found");
}

TEST_F(CustomCelFunctionTests, CookieValueTests) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {"cookie", "fruit=apple"}, {"cookie", "fruit=banana"}, {"cookie", "veg=carrot"}};
  auto cookie_value = cookieValue(&arena, request_headers, CelValue::CreateStringView("fruit"));
  auto value = cookie_value.StringOrDie().value();
  ASSERT_EQ(value, "apple");
  cookie_value = cookieValue(&arena, request_headers, CelValue::CreateStringView("veg"));
  value = cookie_value.StringOrDie().value();
  ASSERT_EQ(value, "carrot");

  auto error = cookieValue(&arena, request_headers, CelValue::CreateStringView("legume"));
  ASSERT_TRUE(error.IsError());
  ASSERT_EQ(error.ErrorOrDie()->message(), "cookieValue() cookie value not found");

  Http::TestRequestHeaderMapImpl no_cookie_request_headers = {{"path", "/query?a=apple"}};
  error = cookieValue(&arena, no_cookie_request_headers, CelValue::CreateStringView("fruit"));
  ASSERT_TRUE(error.IsError());
  ASSERT_EQ(error.ErrorOrDie()->message(), "cookieValue() cookie value not found");
}

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
