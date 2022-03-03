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

using ::google::api::expr::runtime::CelMap;
using google::api::expr::runtime::CelValue;

// tests for the custom CEL functions in the reference implementation

CelValue createStrToIntCelMap(Protobuf::Arena& arena,
                              absl::flat_hash_map<std::string, int64_t> map);

class CustomCelFunctionTests : public testing::Test {
public:
  Protobuf::Arena arena;
  CelValue result;

  void urlFunctionTest(absl::flat_hash_map<std::string, std::string> headers,
                       absl::StatusCode expected_status_code, absl::string_view expected_url) {

    CelValue headers_cel_map = Utility::createCelMap(arena, headers);
    evaluateUrlFunction(headers_cel_map, expected_status_code, expected_url);
  }

  void urlFunctionTestWithBadHeaders(absl::flat_hash_map<std::string, int64_t> bad_headers,
                                     absl::StatusCode expected_status_code,
                                     absl::string_view expected_url) {

    CelValue bad_headers_cel_map = createStrToIntCelMap(arena, bad_headers);
    evaluateUrlFunction(bad_headers_cel_map, expected_status_code, expected_url);
  }

  void evaluateUrlFunction(CelValue headers_cel_map, absl::StatusCode expected_status_code,
                           absl::string_view expected_url) {
    UrlFunction function("url");
    std::vector<CelValue> input_values = {headers_cel_map};
    auto args = absl::Span<CelValue>(input_values);
    absl::Status status = function.Evaluate(args, &result, &arena);
    EXPECT_EQ(status.code(), expected_status_code);
    if (status.ok()) {
      EXPECT_EQ(result.StringOrDie().value(), expected_url);
    } else {
      EXPECT_TRUE(result.IsError());
    }
  }
};

CelValue createStrToIntCelMap(Protobuf::Arena& arena,
                              absl::flat_hash_map<std::string, int64_t> map) {

  std::vector<std::pair<CelValue, CelValue>> key_value_pairs;
  // create vector of key value pairs from cookies map
  for (auto& [key, value] : map) {
    auto key_value_pair =
        std::make_pair(CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena, key)),
                       CelValue::CreateInt64(value));
    key_value_pairs.push_back(key_value_pair);
  }
  std::unique_ptr<CelMap> map_unique_ptr =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(key_value_pairs)).value();
  // transfer ownership of map from unique_ptr to arena
  CelMap* map_raw_ptr = map_unique_ptr.release();
  arena.Own(map_raw_ptr);
  return CelValue::CreateMap(map_raw_ptr);
}

TEST_F(CustomCelFunctionTests, UrlFunctionTests) {
  absl::flat_hash_map<std::string, std::string> map = {{"host", "abc.com:1234"}, {"path", ""}};

  urlFunctionTest(map, absl::StatusCode::kOk, "abc.com:1234");

  map.clear();
  map["host"] = "abc.com:1234";
  urlFunctionTest(map, absl::StatusCode::kNotFound, "");

  map.clear();
  map["path"] = "";
  urlFunctionTest(map, absl::StatusCode::kNotFound, "");

  absl::flat_hash_map<std::string, int64_t> str_to_int_map = {{"host", 1}, {"path", 2}};

  urlFunctionTestWithBadHeaders(str_to_int_map, absl::StatusCode::kNotFound, "");

  map.erase("host");
  urlFunctionTestWithBadHeaders(str_to_int_map, absl::StatusCode::kNotFound, "");
}

TEST_F(CustomCelFunctionTests, CookieTests) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {"cookie", "fruit=apple"}, {"cookie", "fruit=banana"}, {"cookie", "veg=carrot"}};
  auto cookie_map = cookie(&arena, request_headers).MapOrDie();
  auto value = (*cookie_map)[CelValue::CreateStringView("fruit")]->StringOrDie().value();
  EXPECT_EQ(value, "apple");
  value = (*cookie_map)[CelValue::CreateStringView("veg")]->StringOrDie().value();
  EXPECT_EQ(value, "carrot");

  Http::TestRequestHeaderMapImpl no_cookie_request_headers = {{"path", "/query?a=apple"}};
  auto error = cookie(&arena, no_cookie_request_headers);
  ASSERT_TRUE(error.IsError());
  EXPECT_EQ(error.ErrorOrDie()->message(), "cookie() no cookies found");
}

TEST_F(CustomCelFunctionTests, CookieValueTests) {
  Http::TestRequestHeaderMapImpl request_headers = {
      {"cookie", "fruit=apple"}, {"cookie", "fruit=banana"}, {"cookie", "veg=carrot"}};
  auto cookie_value = cookieValue(&arena, request_headers, CelValue::CreateStringView("fruit"));
  auto value = cookie_value.StringOrDie().value();
  EXPECT_EQ(value, "apple");
  cookie_value = cookieValue(&arena, request_headers, CelValue::CreateStringView("veg"));
  value = cookie_value.StringOrDie().value();
  EXPECT_EQ(value, "carrot");

  auto error = cookieValue(&arena, request_headers, CelValue::CreateStringView("legume"));
  ASSERT_TRUE(error.IsError());
  EXPECT_EQ(error.ErrorOrDie()->message(), "cookieValue() cookie value not found");

  Http::TestRequestHeaderMapImpl no_cookie_request_headers = {{"path", "/query?a=apple"}};
  error = cookieValue(&arena, no_cookie_request_headers, CelValue::CreateStringView("fruit"));
  ASSERT_TRUE(error.IsError());
  EXPECT_EQ(error.ErrorOrDie()->message(), "cookieValue() cookie value not found");
}

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
