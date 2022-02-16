#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

using google::api::expr::runtime::CelValue;

// tests for the custom CEL functions in the example implementation

class CustomCELFunctionTests : public testing::Test {
public:
  Protobuf::Arena arena;
  CelValue result;
};

void matchDescriptorsTest(CelFunctionDescriptor descriptor1, CelFunctionDescriptor descriptor2) {
  EXPECT_EQ(descriptor1.name(), descriptor2.name());
  EXPECT_EQ(descriptor1.receiver_style(), descriptor2.receiver_style());
  EXPECT_EQ(descriptor1.types(), descriptor2.types());
}

TEST_F(CustomCELFunctionTests, GetProductTest) {
  GetProduct function("GetProduct");
  std::vector<CelValue> input_values = {CelValue::CreateInt64(2), CelValue::CreateInt64(3)};
  auto args = absl::Span<CelValue>(input_values);
  absl::Status status = function.Evaluate(args, &result, &arena);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(result.Int64OrDie(), 6);
  matchDescriptorsTest(function.createDescriptor("GetProduct"), function.descriptor());
}

TEST_F(CustomCELFunctionTests, GetDoubleTest) {
  GetDouble function("GetDouble");
  std::vector<CelValue> input_values = {CelValue::CreateInt64(2)};
  auto args = absl::Span<CelValue>(input_values);
  absl::Status status = function.Evaluate(args, &result, &arena);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(result.Int64OrDie(), 4);
  matchDescriptorsTest(function.createDescriptor("GetDouble"), function.descriptor());
}

TEST_F(CustomCELFunctionTests, Get99Test) {
  Get99 function("Get99");
  std::vector<CelValue> input_values = {};
  auto args = absl::Span<CelValue>(input_values);
  absl::Status status = function.Evaluate(args, &result, &arena);
  ASSERT_TRUE(status.ok());
  EXPECT_EQ(result.Int64OrDie(), 99);
  matchDescriptorsTest(function.createDescriptor("Get99"), function.descriptor());
}

TEST_F(CustomCELFunctionTests, GetSquareOfTest) {
  EXPECT_EQ(getSquareOf(&arena, 4).Int64OrDie(), 16);
}

TEST_F(CustomCELFunctionTests, GetNextIntTest) {
  EXPECT_EQ(getNextInt(&arena, 10).Int64OrDie(), 11);
}

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
