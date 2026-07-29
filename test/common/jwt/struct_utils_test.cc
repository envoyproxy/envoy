// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <vector>

#include "source/common/jwt/struct_utils.h"
#include "source/common/protobuf/protobuf.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

Protobuf::Struct makeStruct(const std::string& json) {
  Protobuf::Struct struct_pb;
  TestUtility::loadFromJson(json, struct_pb);
  return struct_pb;
}

// The payload from https://github.com/envoyproxy/envoy/issues/33603.
constexpr char UrlClaimPayload[] = R"(
{
  "iss": "http://example.org/",
  "sub": "johndoe@example.org",
  "flavour": "chocolate",
  "parent_token": "abc",
  "some_url_value": "http://example.org/about",
  "http://example.org/parent_token": "xyz"
}
)";

constexpr char NestedPayload[] = R"(
{
  "sub": "test@example.com",
  "nested": {
    "key-1": "value1",
    "nested-2": {
      "key-2": "value2",
      "key-3": true,
      "key-4": 9999,
      "key-5": ["str1", "str2"],
      "key-6": 1.5
    }
  }
}
)";

// ---------------------------------------------------------------------------
// GetValue: nested walk only.
// ---------------------------------------------------------------------------

TEST(StructUtilsTest, GetValueNameWithoutDots) {
  const auto payload = makeStruct(NestedPayload);
  StructUtils utils(payload);

  std::string value;
  EXPECT_EQ(StructUtils::OK, utils.GetString("sub", &value));
  EXPECT_EQ("test@example.com", value);
}

TEST(StructUtilsTest, GetValueNestedLookup) {
  const auto payload = makeStruct(NestedPayload);
  StructUtils utils(payload);

  std::string value;
  EXPECT_EQ(StructUtils::OK, utils.GetString("nested.key-1", &value));
  EXPECT_EQ("value1", value);

  EXPECT_EQ(StructUtils::OK, utils.GetString("nested.nested-2.key-2", &value));
  EXPECT_EQ("value2", value);

  const auto abc = makeStruct(R"({"a": {"b": {"c": "deep"}}})");
  StructUtils abc_utils(abc);
  EXPECT_EQ(StructUtils::OK, abc_utils.GetString("a.b.c", &value));
  EXPECT_EQ("deep", value);
}

// The mechanism behind issue #33603: the fallback that fixes it lives in the caller, not here.
TEST(StructUtilsTest, GetValueDoesNotMatchLiteralKeyWithDots) {
  const auto payload = makeStruct(UrlClaimPayload);
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::MISSING, utils.GetValue("http://example.org/parent_token", found));

  const auto only_literal = makeStruct(R"({"a.b": "from-literal"})");
  StructUtils only_literal_utils(only_literal);
  EXPECT_EQ(StructUtils::MISSING, only_literal_utils.GetValue("a.b", found));
}

TEST(StructUtilsTest, GetValueMissingClaim) {
  const auto payload = makeStruct(NestedPayload);
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::MISSING, utils.GetValue("no_such_claim", found));
  EXPECT_EQ(StructUtils::MISSING, utils.GetValue("nested.no_such_claim", found));
  EXPECT_EQ(StructUtils::MISSING, utils.GetValue("nested.nested-2.no_such_claim", found));
}

// WRONG_TYPE is distinct from MISSING; the jwt_authn filter relies on it surviving unmasked.
TEST(StructUtilsTest, GetValueWrongTypeIntermediate) {
  const auto payload = makeStruct(R"({"a": "scalar"})");
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::WRONG_TYPE, utils.GetValue("a.b", found));
}

// ---------------------------------------------------------------------------
// GetLiteralValue: exact whole-string top-level key only.
// ---------------------------------------------------------------------------

TEST(StructUtilsTest, GetLiteralValueWithDots) {
  const auto payload = makeStruct(UrlClaimPayload);
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("http://example.org/parent_token", found));
  EXPECT_EQ("xyz", found->string_value());
}

TEST(StructUtilsTest, GetLiteralValueSiblingClaims) {
  const auto payload = makeStruct(UrlClaimPayload);
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("parent_token", found));
  EXPECT_EQ("abc", found->string_value());

  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("some_url_value", found));
  EXPECT_EQ("http://example.org/about", found->string_value());

  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("flavour", found));
  EXPECT_EQ("chocolate", found->string_value());
}

TEST(StructUtilsTest, GetLiteralValueDoesNotWalkNestedPaths) {
  const auto payload = makeStruct(NestedPayload);
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::MISSING, utils.GetLiteralValue("nested.key-1", found));
  EXPECT_EQ(StructUtils::MISSING, utils.GetLiteralValue("nested.nested-2.key-2", found));

  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("nested", found));
  EXPECT_EQ(Protobuf::Value::kStructValue, found->kind_case());
}

// A dot-free name resolves identically through both methods, which is why the caller can skip
// the fallback entirely when there is no dot.
TEST(StructUtilsTest, GetLiteralValueAgreesWithGetValueWithoutDots) {
  const auto payload = makeStruct(NestedPayload);
  StructUtils utils(payload);

  const Protobuf::Value* nested_found = nullptr;
  const Protobuf::Value* literal_found = nullptr;
  EXPECT_EQ(StructUtils::OK, utils.GetValue("sub", nested_found));
  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("sub", literal_found));
  EXPECT_EQ(nested_found, literal_found);

  EXPECT_EQ(StructUtils::MISSING, utils.GetValue("no_such_claim", nested_found));
  EXPECT_EQ(StructUtils::MISSING, utils.GetLiteralValue("no_such_claim", literal_found));
}

// GetLiteralValue has no notion of an intermediate element, so it never produces WRONG_TYPE.
TEST(StructUtilsTest, GetLiteralValueMissingClaim) {
  const auto payload = makeStruct(R"({"a": "scalar"})");
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::MISSING, utils.GetLiteralValue("a.b", found));
  EXPECT_EQ(StructUtils::MISSING, utils.GetLiteralValue("no_such_claim", found));
}

// PRECEDENCE MECHANISM: when a payload holds both a literal "a.b" key and a nested a -> {b}, the
// two methods address the two different values. A caller layering GetLiteralValue behind GetValue
// therefore keeps resolving "a.b" to the nested value, so no config which resolves today changes
// meaning.
TEST(StructUtilsTest, NestedAndLiteralKeysAreDistinctlyAddressable) {
  const auto payload = makeStruct(R"({"a": {"b": "from-nested"}, "a.b": "from-literal"})");
  StructUtils utils(payload);

  const Protobuf::Value* found = nullptr;
  EXPECT_EQ(StructUtils::OK, utils.GetValue("a.b", found));
  EXPECT_EQ("from-nested", found->string_value());

  EXPECT_EQ(StructUtils::OK, utils.GetLiteralValue("a.b", found));
  EXPECT_EQ("from-literal", found->string_value());
}

// ---------------------------------------------------------------------------
// Typed getters: these route through GetValue, so they are nested-walk only.
// ---------------------------------------------------------------------------

TEST(StructUtilsTest, TypedGettersWithNestedNames) {
  const auto payload = makeStruct(NestedPayload);
  StructUtils utils(payload);

  std::string str_value;
  EXPECT_EQ(StructUtils::OK, utils.GetString("nested.nested-2.key-2", &str_value));
  EXPECT_EQ("value2", str_value);

  bool bool_value = false;
  EXPECT_EQ(StructUtils::OK, utils.GetBoolean("nested.nested-2.key-3", &bool_value));
  EXPECT_TRUE(bool_value);

  uint64_t int_value = 0;
  EXPECT_EQ(StructUtils::OK, utils.GetUInt64("nested.nested-2.key-4", &int_value));
  EXPECT_EQ(9999, int_value);

  double double_value = 0;
  EXPECT_EQ(StructUtils::OK, utils.GetDouble("nested.nested-2.key-6", &double_value));
  EXPECT_EQ(1.5, double_value);

  std::vector<std::string> list_value;
  EXPECT_EQ(StructUtils::OK, utils.GetStringList("nested.nested-2.key-5", &list_value));
  EXPECT_EQ(std::vector<std::string>({"str1", "str2"}), list_value);
}

// Every production caller of the typed getters passes a hardcoded dot-free registered name
// (alg/kid/iss/sub/iat/nbf/exp/jti/aud, and the JWK fields), so none needs the literal fallback.
TEST(StructUtilsTest, TypedGettersDoNotMatchLiteralKeysWithDots) {
  const auto payload = makeStruct(R"(
{
  "http://example.org/string": "xyz",
  "http://example.org/bool": true,
  "http://example.org/int": 9999,
  "http://example.org/double": 1.5,
  "http://example.org/list": ["str1", "str2"]
}
)");
  StructUtils utils(payload);

  std::string str_value;
  EXPECT_EQ(StructUtils::MISSING, utils.GetString("http://example.org/string", &str_value));

  bool bool_value = false;
  EXPECT_EQ(StructUtils::MISSING, utils.GetBoolean("http://example.org/bool", &bool_value));

  uint64_t int_value = 0;
  EXPECT_EQ(StructUtils::MISSING, utils.GetUInt64("http://example.org/int", &int_value));

  double double_value = 0;
  EXPECT_EQ(StructUtils::MISSING, utils.GetDouble("http://example.org/double", &double_value));

  std::vector<std::string> list_value;
  EXPECT_EQ(StructUtils::MISSING, utils.GetStringList("http://example.org/list", &list_value));
}

TEST(StructUtilsTest, TypedGettersWrongType) {
  const auto payload = makeStruct(R"({"str": "xyz", "num": 1.5})");
  StructUtils utils(payload);

  bool bool_value = false;
  EXPECT_EQ(StructUtils::WRONG_TYPE, utils.GetBoolean("str", &bool_value));

  double double_value = 0;
  EXPECT_EQ(StructUtils::WRONG_TYPE, utils.GetDouble("str", &double_value));

  std::string str_value;
  EXPECT_EQ(StructUtils::WRONG_TYPE, utils.GetString("num", &str_value));
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
