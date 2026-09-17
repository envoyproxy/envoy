#pragma once

#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "gmock/gmock.h"

namespace Envoy {

MATCHER_P(IsStructValueString, expected, "") {
  if (arg.kind_case() != Protobuf::Value::kStringValue) {
    *result_listener << "which has kind " << arg.kind_case();
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.string_value(), result_listener);
}

MATCHER_P(IsStructValueNumber, expected, "") {
  if (arg.kind_case() != Protobuf::Value::kNumberValue) {
    *result_listener << "which has kind " << arg.kind_case();
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.number_value(), result_listener);
}

MATCHER_P(IsStructValueBool, expected, "") {
  if (arg.kind_case() != Protobuf::Value::kBoolValue) {
    *result_listener << "which has kind " << arg.kind_case();
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.bool_value(), result_listener);
}

MATCHER_P(IsStructValueNull, expected, "") {
  if (arg.kind_case() != Protobuf::Value::kNullValue) {
    *result_listener << "which has kind " << arg.kind_case();
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.null_value(), result_listener);
}

MATCHER_P(IsStructValueStruct, expected, "") {
  if (arg.kind_case() != Protobuf::Value::kStructValue) {
    *result_listener << "which has kind " << arg.kind_case();
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.struct_value().fields(), result_listener);
}

MATCHER_P(IsStructValueList, expected, "") {
  if (arg.kind_case() != Protobuf::Value::kListValue) {
    *result_listener << "which has kind " << arg.kind_case();
    return false;
  }
  return ::testing::ExplainMatchResult(expected, arg.list_value().values(), result_listener);
}

#define DEFINE_STRUCT_MATCHER(name, value_matcher)                                                 \
  MATCHER_P2(name, key, expected, "") {                                                            \
    return ::testing::ExplainMatchResult(key, arg.first, result_listener) &&                       \
           ::testing::ExplainMatchResult(value_matcher(expected), arg.second, result_listener);    \
  }

DEFINE_STRUCT_MATCHER(IsStructString, IsStructValueString)
DEFINE_STRUCT_MATCHER(IsStructNumber, IsStructValueNumber)
DEFINE_STRUCT_MATCHER(IsStructBool, IsStructValueBool)
DEFINE_STRUCT_MATCHER(IsStructNull, IsStructValueNull)
DEFINE_STRUCT_MATCHER(IsStructStruct, IsStructValueStruct)
DEFINE_STRUCT_MATCHER(IsStructList, IsStructValueList)

#undef DEFINE_STRUCT_MATCHER

MATCHER_P2(IsStructField, key, expected, "") {
  return ::testing::ExplainMatchResult(key, arg.first, result_listener) &&
         ::testing::ExplainMatchResult(expected, arg.second.fields(), result_listener);
}

MATCHER_P(HasStructFields, expected, "") {
  return ::testing::ExplainMatchResult(expected, arg.fields(), result_listener);
}

using StructField = Protobuf::MapPair<std::string, Protobuf::Value>;

template <typename... MatcherT>
std::vector<::testing::Matcher<StructField>> StructMatchers(MatcherT... matchers) {
  return {::testing::SafeMatcherCast<StructField>(matchers)...};
}

} // namespace Envoy
