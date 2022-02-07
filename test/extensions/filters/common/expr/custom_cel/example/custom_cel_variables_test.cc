#include "source/common/http/utility.h"
#include "source/common/network/address_impl.h"
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

class CustomVariablesTests : public testing::Test {
public:
  Protobuf::Arena arena;
  StreamInfo::MockStreamInfo mock_stream_info;
};

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

class SourceVariablesTests : public testing::Test {
public:
  Protobuf::Arena arena;
  StreamInfo::MockStreamInfo mock_stream_info;
};

TEST_F(SourceVariablesTests, ListKeysTest) {
  SourceWrapper source_vars(arena, mock_stream_info);
  EXPECT_EQ(source_vars.ListKeys()->size(), SourceList.size());
}

TEST_F(SourceVariablesTests, AddressPortDescriptionTest) {
  mock_stream_info.downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 80));
  EXPECT_CALL(mock_stream_info, downstreamAddressProvider()).Times(3);
  SourceWrapper source_vars(arena, mock_stream_info);
  auto value = source_vars[CelValue::CreateStringView("address")];
  EXPECT_EQ(value->StringOrDie().value(), "0.0.0.0:80");
  value = source_vars[CelValue::CreateStringView("port")];
  EXPECT_EQ(value->Int64OrDie(), 80);
  value = source_vars[CelValue::CreateStringView("description")];
  EXPECT_EQ(value->StringOrDie().value(), "description: has address, port values");
}

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
