#include <string>

#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

nlohmann::json parse(const std::string& json) {
  nlohmann::json result = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  EXPECT_FALSE(result.is_discarded()) << json;
  return result;
}

TEST(ReadStringTest, ReadsUsableValues) {
  bool malformed = false;
  EXPECT_EQ(readString(parse(R"({"k":"v"})"), "k", malformed), "v");
  EXPECT_FALSE(malformed);
}

// Absent, null and empty are all "the request did not say", not a defect.
TEST(ReadStringTest, BenignlyAbsentValuesDoNotFlag) {
  for (const std::string json : {"{}", R"({"k":null})", R"({"k":""})"}) {
    bool malformed = false;
    EXPECT_FALSE(readString(parse(json), "k", malformed).has_value()) << json;
    EXPECT_FALSE(malformed) << json;
  }
}

TEST(ReadStringTest, UnusableValuesReadAsAbsentAndFlag) {
  for (const std::string json : {R"({"k":42})", R"({"k":true})", R"({"k":[]})", R"({"k":{}})"}) {
    bool malformed = false;
    EXPECT_FALSE(readString(parse(json), "k", malformed).has_value()) << json;
    EXPECT_TRUE(malformed) << json;
  }

  bool malformed = false;
  const std::string oversized = R"({"k":")" + std::string(MaxStringValueSize + 1, 'v') + R"("})";
  EXPECT_FALSE(readString(parse(oversized), "k", malformed).has_value());
  EXPECT_TRUE(malformed);
}

// An offloaded string is a binary node, so it is not readable from the index.
TEST(ReadStringTest, ExternalReferenceReadsAsAbsentAndFlags) {
  nlohmann::json json = parse("{}");
  json["k"] = JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 20});
  bool malformed = false;
  EXPECT_FALSE(readString(json, "k", malformed).has_value());
  EXPECT_TRUE(malformed);
}

// The overload reads the same value; only the diagnostic differs, which is what
// keeps a response-path shape probe from flagging an unrelated document.
TEST(ReadStringTest, OverloadWithoutFlagReadsIdenticalValues) {
  EXPECT_EQ(readString(parse(R"({"k":"v"})"), "k"), "v");
  for (const std::string json : {"{}", R"({"k":null})", R"({"k":""})", R"({"k":42})"}) {
    EXPECT_FALSE(readString(parse(json), "k").has_value()) << json;
  }
}

TEST(ReadBoolTest, ReadsBothValuesAndFlagsOnlyWrongTypes) {
  bool malformed = false;
  EXPECT_EQ(readBool(parse(R"({"k":true})"), "k", malformed), true);
  EXPECT_EQ(readBool(parse(R"({"k":false})"), "k", malformed), false);
  EXPECT_FALSE(readBool(parse("{}"), "k", malformed).has_value());
  EXPECT_FALSE(readBool(parse(R"({"k":null})"), "k", malformed).has_value());
  EXPECT_FALSE(malformed);

  EXPECT_FALSE(readBool(parse(R"({"k":"true"})"), "k", malformed).has_value());
  EXPECT_TRUE(malformed);
}

TEST(ReadArrayLengthTest, CountsElementsWithoutValidatingThem) {
  bool malformed = false;
  EXPECT_EQ(readArrayLength(parse(R"({"k":[]})"), "k", malformed), 0u);
  EXPECT_EQ(readArrayLength(parse(R"({"k":[1,"two",{},null]})"), "k", malformed), 4u);
  EXPECT_FALSE(readArrayLength(parse("{}"), "k", malformed).has_value());
  EXPECT_FALSE(readArrayLength(parse(R"({"k":null})"), "k", malformed).has_value());
  EXPECT_FALSE(malformed);

  EXPECT_FALSE(readArrayLength(parse(R"({"k":{}})"), "k", malformed).has_value());
  EXPECT_TRUE(malformed);
}

// Null is a documented "unset" on the request path and a defect on the response
// path, so the caller picks.
TEST(ReadCountTest, NullPolicyDecidesWhetherNullIsMalformed) {
  bool malformed = false;
  EXPECT_FALSE(readCount(parse(R"({"k":null})"), "k", malformed, NullPolicy::AllowNullAsAbsent)
                   .has_value());
  EXPECT_FALSE(malformed);

  EXPECT_FALSE(readCount(parse(R"({"k":null})"), "k", malformed).has_value());
  EXPECT_TRUE(malformed);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
