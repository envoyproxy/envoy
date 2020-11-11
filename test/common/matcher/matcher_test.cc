#include <memory>

#include "envoy/config/core/v3/extension.pb.h"

#include "common/matcher/matcher.h"
#include "common/protobuf/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

struct TestInput : public DataInput {
  absl::string_view get(const MatchingData&) { return "foo"; }
};

struct TestData : public MatchingData {};

TEST(Matcher, MultimapMatcher) {
  google::protobuf::StringValue string_value;
  string_value.set_value("no_match");

  envoy::config::core::v3::TypedExtensionConfig no_match;
  no_match.mutable_typed_config()->PackFrom(string_value);
  MultimapMatcher matcher(std::make_unique<TestInput>(), std::make_pair(no_match, nullptr));

  TestData data;
  const auto result = matcher.match(data);
  EXPECT_TRUE(result.first);
  EXPECT_TRUE(result.second.has_value());

  EXPECT_EQ(nullptr, result.second->second);
  EXPECT_TRUE(result.second->first.has_value());

  google::protobuf::StringValue result_value;
  MessageUtil::unpackTo(result.second->first->typed_config(), result_value);
  EXPECT_TRUE(TestUtility::protoEqual(result_value, string_value));
}

} // namespace Envoy