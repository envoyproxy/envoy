#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/types/optional.h"
#include "eval/public/cel_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

class CustomCelVariablesTests : public testing::Test {
public:
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  Protobuf::Arena arena;
};

TEST_F(CustomCelVariablesTests, ListKeysTest) {
  StreamInfo::MockStreamInfo mock_stream_info;
  CustomCelVariablesWrapper custom_cel_vars(arena, mock_stream_info, &request_headers,
                                            &response_headers, &response_trailers);
  EXPECT_EQ(custom_cel_vars.ListKeys()->size(), CustomCelVariablesList.size());
}

TEST_F(CustomCelVariablesTests, IncorrectIntegerKeyTest) {
  StreamInfo::MockStreamInfo mock_stream_info;
  CustomCelVariablesWrapper custom_cel_vars(arena, mock_stream_info, &request_headers,
                                            &response_headers, &response_trailers);
  auto value = custom_cel_vars[CelValue::CreateInt64(1)];
  ASSERT_FALSE(value.has_value());
}

TEST_F(CustomCelVariablesTests, TeamTest) {
  StreamInfo::MockStreamInfo mock_stream_info;
  CustomCelVariablesWrapper custom_cel_vars(arena, mock_stream_info, &request_headers,
                                            &response_headers, &response_trailers);
  auto value = custom_cel_vars[CelValue::CreateStringView("team")];
  EXPECT_EQ(value->StringOrDie().value(), "spirit");
}

TEST_F(CustomCelVariablesTests, ProtocolIsNullTest) {
  StreamInfo::MockStreamInfo mock_stream_info;
  CustomCelVariablesWrapper custom_cel_vars(arena, mock_stream_info, &request_headers,
                                            &response_headers, &response_trailers);
  EXPECT_CALL(mock_stream_info, protocol());
  auto value = custom_cel_vars[CelValue::CreateStringView("protocol")];
  ASSERT_FALSE(value.has_value());
}

TEST_F(CustomCelVariablesTests, ProtocolIsHttp10Test) {
  StreamInfo::MockStreamInfo mock_stream_info;
  mock_stream_info.protocol_ = Http::Protocol::Http10;
  CustomCelVariablesWrapper custom_cel_vars(arena, mock_stream_info, &request_headers,
                                            &response_headers, &response_trailers);
  EXPECT_CALL(mock_stream_info, protocol()).Times(2);
  auto value = custom_cel_vars[CelValue::CreateStringView("protocol")];
  EXPECT_EQ(value->StringOrDie().value(), "HTTP/1.0");
}

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
