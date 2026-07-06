#include <optional>

#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

namespace {

TEST(AwsArn, ValidArn) {
  constexpr auto input_arn = "arn:aws:lambda:us-west-2:1337:function:fun";
  const std::optional<Arn> arn = parseArn(input_arn);
  ASSERT_TRUE(arn.has_value());
  EXPECT_STREQ("aws", arn->partition().c_str());
  EXPECT_STREQ("lambda", arn->service().c_str());
  EXPECT_STREQ("us-west-2", arn->region().c_str());
  EXPECT_STREQ("1337", arn->accountId().c_str());
  EXPECT_STREQ("function", arn->resourceType().c_str());
  EXPECT_STREQ("fun", arn->functionName().c_str());
}

TEST(AwsArn, ValidArnWithVersion) {
  constexpr auto input_arn = "arn:aws:lambda:us-west-2:1337:function:fun:v2";
  const std::optional<Arn> arn = parseArn(input_arn);
  ASSERT_TRUE(arn.has_value());
  EXPECT_STREQ("aws", arn->partition().c_str());
  EXPECT_STREQ("lambda", arn->service().c_str());
  EXPECT_STREQ("us-west-2", arn->region().c_str());
  EXPECT_STREQ("1337", arn->accountId().c_str());
  EXPECT_STREQ("function", arn->resourceType().c_str());
  EXPECT_STREQ("fun:v2", arn->functionName().c_str());
}

TEST(AwsArn, InvalidArn) {
  constexpr auto input_arn = "arn:aws:lambda:us-west-2:1337:function";
  const std::optional<Arn> arn = parseArn(input_arn);
  EXPECT_EQ(std::nullopt, arn);
}

} // namespace
} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
