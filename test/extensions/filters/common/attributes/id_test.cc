#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/common/expr/cel_state.h"
#include "source/extensions/filters/common/expr/context.h"

#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/host.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {
namespace {

constexpr absl::string_view Undefined = "undefined";

TEST(AttributeId, EmptyHeadersAttributes) {
  Protobuf::Arena arena;
  HeadersWrapper<Http::RequestHeaderMap> headers(arena, nullptr);
  auto header = headers[CelValue::CreateStringView(Referer)];
  EXPECT_FALSE(header.has_value());
  EXPECT_EQ(0, headers.size());
  EXPECT_TRUE(headers.empty());
}
} // namespace
} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy