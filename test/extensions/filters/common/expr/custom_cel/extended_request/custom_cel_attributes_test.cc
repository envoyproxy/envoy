#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_attributes.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/utility/utility.h"

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
  const std::string PATH_STRING =
      R"EOF(/query?a=apple&a=apricot&b=banana&=&cantaloupe&=mango&m=&c=cranberry&)EOF";

  absl::string_view getQueryString(absl::string_view path) {
    auto query_offset = path.find('?');
    if (query_offset != absl::string_view::npos) {
      return path.substr(query_offset + 1);
    }
    return "";
  }
};

TEST_F(ExtendedRequestAttributesTests, ListKeysTest) {
  ExtendedRequestWrapper extended_request_vars(arena, nullptr, mock_stream_info, false);
  RequestWrapper request_vars(arena, nullptr, mock_stream_info);
  auto merged_list = Utility::mergeLists(arena, request_vars.ListKeys(), &ExtendedRequestList);
  size_t merged_list_size = merged_list->size();
  EXPECT_EQ(extended_request_vars.ListKeys()->size(), merged_list_size);
}

TEST_F(ExtendedRequestAttributesTests, NonStringKeyTest) {
  ExtendedRequestWrapper extended_request_vars(arena, nullptr, mock_stream_info, false);
  auto value = extended_request_vars[CelValue::CreateInt64(0)];
  EXPECT_FALSE(value.has_value());
}

TEST_F(ExtendedRequestAttributesTests, EmptyQueryStringTest) {
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/path"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, true);
  auto query_params = extended_request_vars[CelValue::CreateStringView("query")];
  EXPECT_TRUE(query_params->IsMap() && query_params->MapOrDie()->size() == 0);
}

TEST_F(ExtendedRequestAttributesTests, DeferToBaseClassTest) {
  // ExtendedRequestWrapper doesn't have the "method" key.
  // Should fall through to RequestWrapper for "method"
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("method")]->StringOrDie().value();
  EXPECT_EQ(value, "GET");
}

TEST_F(ExtendedRequestAttributesTests, NoPathTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, true);
  bool isError = extended_request_vars[CelValue::CreateStringView("query")]->IsError();
  EXPECT_TRUE(isError);
  auto error = extended_request_vars[CelValue::CreateStringView("query")]->ErrorOrDie();
  EXPECT_EQ(error->message(), "request.query path missing");
}

TEST_F(ExtendedRequestAttributesTests, QueryAsStringDeferToBaseClassTest) {
  Http::TestRequestHeaderMapImpl request_headers{{":path", PATH_STRING}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  absl::string_view extended_request_query =
      extended_request_vars[CelValue::CreateStringView("query")]->StringOrDie().value();
  auto query = getQueryString(PATH_STRING);
  EXPECT_EQ(extended_request_query, query);
  RequestWrapper request_vars(arena, &request_headers, mock_stream_info);
  absl::string_view request_query =
      request_vars[CelValue::CreateStringView("query")]->StringOrDie().value();
  EXPECT_EQ(extended_request_query, request_query);
}

TEST_F(ExtendedRequestAttributesTests, QueryAsMapTest) {
  // path - query portion - contains correct key value parameters and incorrect key value parameters
  Http::TestRequestHeaderMapImpl request_headers{{":path", PATH_STRING}};
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
