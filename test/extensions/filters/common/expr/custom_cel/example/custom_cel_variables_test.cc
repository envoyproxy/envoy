#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

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
namespace Custom_CEL {
namespace Example {

class VariablesTest : public testing::Test {
public:
  Protobuf::Arena arena;
  StreamInfo::MockStreamInfo mock_stream_info;
};

class CustomVariablesTests : public VariablesTest {};

TEST_F(CustomVariablesTests, ListKeysTest) {
  CustomWrapper custom_vars(arena, mock_stream_info);
  EXPECT_EQ(custom_vars.ListKeys()->size(), CustomList.size());
}

TEST_F(CustomVariablesTests, IncorrectIntegerKeyTest) {
  CustomWrapper custom_vars(arena, mock_stream_info);
  auto value = custom_vars[CelValue::CreateInt64(1)];
  ASSERT_FALSE(value.has_value());
}

TEST_F(CustomVariablesTests, TeamTest) {
  CustomWrapper custom_vars(arena, mock_stream_info);
  auto value = custom_vars[CelValue::CreateStringView("team")];
  EXPECT_EQ(value->StringOrDie().value(), "spirit");
}

TEST_F(CustomVariablesTests, NonexistentKeyTest) {
  CustomWrapper custom_vars(arena, mock_stream_info);
  auto value = custom_vars[CelValue::CreateStringView("nonexistent")];
  ASSERT_FALSE(value.has_value());
}

TEST_F(CustomVariablesTests, ProtocolIsNullTest) {
  CustomWrapper custom_vars(arena, mock_stream_info);
  EXPECT_CALL(mock_stream_info, protocol());
  auto value = custom_vars[CelValue::CreateStringView("protocol")];
  ASSERT_FALSE(value.has_value());
}

TEST_F(CustomVariablesTests, ProtocolIsHttp10Test) {
  mock_stream_info.protocol_ = Http::Protocol::Http10;
  CustomWrapper custom_vars(arena, mock_stream_info);
  EXPECT_CALL(mock_stream_info, protocol()).Times(2);
  auto value = custom_vars[CelValue::CreateStringView("protocol")];
  EXPECT_EQ(value->StringOrDie().value(), "HTTP/1.0");
}

class SourceVariablesTests : public VariablesTest {};

TEST_F(SourceVariablesTests, ListKeysTest) {
  SourceWrapper source_vars(arena, mock_stream_info);
  PeerWrapper peer_vars(mock_stream_info, false);
  EXPECT_EQ(source_vars.ListKeys()->size(), SourceList.size() + peer_vars.ListKeys()->size());
}

TEST_F(SourceVariablesTests, AddressPortDescriptionTest) {
  // incorrect integer key
  SourceWrapper source_vars(arena, mock_stream_info);
  auto value = source_vars[CelValue::CreateInt64(1)];
  ASSERT_FALSE(value.has_value());
  value = source_vars[CelValue::CreateStringView("nonexistent")];
  ASSERT_FALSE(value.has_value());

  mock_stream_info.downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 80));
  EXPECT_CALL(mock_stream_info, downstreamAddressProvider()).Times(3);
  SourceWrapper source_vars2(arena, mock_stream_info);
  value = source_vars2[CelValue::CreateStringView("address")];
  EXPECT_EQ(value->StringOrDie().value(), "0.0.0.0:80");
  value = source_vars2[CelValue::CreateStringView("port")];
  EXPECT_EQ(value->Int64OrDie(), 80);
  value = source_vars2[CelValue::CreateStringView("description")];
  EXPECT_EQ(value->StringOrDie().value(), "description: has address, port values");
}

class ExtendedRequestVariablesTests : public VariablesTest {
public:
};

TEST_F(ExtendedRequestVariablesTests, ListKeysTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  RequestWrapper request_vars(arena, &request_headers, mock_stream_info);
  EXPECT_EQ(extended_request_vars.ListKeys()->size(),
            ExtendedRequestList.size() + request_vars.ListKeys()->size());
}

TEST_F(ExtendedRequestVariablesTests, EmptyQueryStringTest) {
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/query?"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("query")]->StringOrDie().value();
  EXPECT_EQ(value, "");
}

TEST_F(ExtendedRequestVariablesTests, GetMethodTest) {
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  // Should go to RequestWrapper for "method"
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("method")]->StringOrDie().value();
  EXPECT_EQ(value, "GET");
}

TEST_F(ExtendedRequestVariablesTests, QueryAsStringTest) {
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/query?a=apple&a=apricot&b=banana&=&cantaloupe&=mango&m=&c=cranberry&"}};
  ExtendedRequestWrapper extended_request_vars(arena, &request_headers, mock_stream_info, false);
  absl::string_view value =
      extended_request_vars[CelValue::CreateStringView("query")]->StringOrDie().value();
  EXPECT_EQ(value, "a=apple&a=apricot&b=banana&=&cantaloupe&=mango&m=&c=cranberry&");
}

TEST_F(ExtendedRequestVariablesTests, QueryAsMapTest) {
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

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
