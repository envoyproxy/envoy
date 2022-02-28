#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_attributes.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_map_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

// tests for the custom CEL attributes in the reference implementation

class ExtendedRequestAttributesTests : public testing::Test {
public:
  Protobuf::Arena arena;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info;
};

TEST_F(ExtendedRequestAttributesTests, ListKeysTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  RequestWrapper request_vars(arena, &request_headers, mock_stream_info);
  EXPECT_EQ(extended_request_vars.ListKeys()->size(),
            ExtendedRequestList.size() + request_vars.ListKeys()->size());
}

TEST_F(ExtendedRequestAttributesTests, EmptyQueryStringTest) {
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/query?"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("query")]->StringOrDie().value();
  EXPECT_EQ(value, "");
}

TEST_F(ExtendedRequestAttributesTests, GetMethodTest) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  // Should fall through to RequestWrapper for "method"
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("method")]->StringOrDie().value();
  EXPECT_EQ(value, "GET");
}

TEST_F(ExtendedRequestAttributesTests, QueryWithNoPathTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  bool isError = extended_request_vars[CelValue::CreateStringView("query")]->IsError();
  ASSERT_TRUE(isError);
}

TEST_F(ExtendedRequestAttributesTests, QueryAsStringTest) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/query?a=apple&a=apricot&b=banana&=&cantaloupe&=mango&m=&c=cranberry&"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("query")]->StringOrDie().value();
  EXPECT_EQ(value, "a=apple&a=apricot&b=banana&=&cantaloupe&=mango&m=&c=cranberry&");
}

TEST_F(ExtendedRequestAttributesTests, QueryAsMapTest) {
  // path - query portion - contains correct key value parameters and incorrect key value parameters
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/query?a=apple&a=apricot&b=banana&=&cantaloupe&=mango&m=&c=cranberry&"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, true);
  auto cel_map = extended_request_vars[CelValue::CreateStringView("query")]->MapOrDie();
  auto value = (*cel_map)[CelValue::CreateStringView("a")]->StringOrDie().value();
  EXPECT_EQ(value, "apple");
  value = (*cel_map)[CelValue::CreateStringView("b")]->StringOrDie().value();
  EXPECT_EQ(value, "banana");
  value = (*cel_map)[CelValue::CreateStringView("c")]->StringOrDie().value();
  EXPECT_EQ(value, "cranberry");
  value = (*cel_map)[CelValue::CreateStringView("m")]->StringOrDie().value();
  EXPECT_EQ(value, "");
}

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
