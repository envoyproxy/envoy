#include <sstream>

#include "test/test_common/struct_matchers.h"

namespace Envoy {

using testing::AllOf;
using testing::Contains;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsSupersetOf;
using testing::UnorderedElementsAre;

template <typename T, typename MatcherT>
std::string expectThatOutput(const T& value, MatcherT matcher) {
  auto m = ::testing::SafeMatcherCast<T>(matcher);
  if (m.Matches(value)) {
    return "";
  }
  std::stringstream ss;
  ss << "Expected: ";
  m.DescribeTo(&ss);
  ss << "\nActual: " << ::testing::PrintToString(value);
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(matcher, value, &listener);
  ss << "\n" << listener.str();
  return ss.str();
}

Protobuf::Struct makeTestStruct() {
  Protobuf::Struct value;
  (*value.mutable_fields())["key1"].set_string_value("value1");
  (*value.mutable_fields())["key2"].set_string_value("value2");
  return value;
}

TEST(StructMatchersTest, MatchesValues) {
  const auto value = makeTestStruct();

  EXPECT_THAT(value.fields(), UnorderedElementsAre(IsStructString("key1", "value1"),
                                                   IsStructString("key2", "value2")));
  EXPECT_THAT(value.fields().at("key1"), IsStructValueString("value1"));
  EXPECT_THAT(value.fields().at("key2"), IsStructValueString("value2"));
  EXPECT_THAT(value.fields(), Contains(IsStructString(HasSubstr("key"), HasSubstr("value"))));
}

TEST(StructMatchersTest, MatchesStructFields) {
  Protobuf::Map<std::string, Protobuf::Struct> metadata;
  (*metadata["someKey"].mutable_fields())["apiIdentifier"].set_string_value("test-api");
  (*metadata["someKey"].mutable_fields())["extHost"].set_string_value("test-host");

  EXPECT_THAT(metadata,
              Contains(IsStructField(
                  "someKey", UnorderedElementsAre(IsStructString("apiIdentifier", "test-api"),
                                                  IsStructString("extHost", "test-host")))));
}

TEST(StructMatchersTest, MatchesValueTypes) {
  Protobuf::Struct value = makeTestStruct();
  (*value.mutable_fields())["number"].set_number_value(1.5);
  (*value.mutable_fields())["bool"].set_bool_value(true);
  (*value.mutable_fields())["null"].set_null_value(Protobuf::NULL_VALUE);

  auto* nested = (*value.mutable_fields())["struct"].mutable_struct_value();
  (*nested->mutable_fields())["nested_key"].set_string_value("nested_value");

  auto* list = (*value.mutable_fields())["list"].mutable_list_value();
  list->add_values()->set_string_value("list_string");
  list->add_values()->set_number_value(2);
  list->add_values()->set_bool_value(false);
  list->add_values()->set_null_value(Protobuf::NULL_VALUE);

  EXPECT_THAT(value.fields(),
              UnorderedElementsAre(
                  IsStructString(HasSubstr("key1"), HasSubstr("value1")),
                  IsStructString("key2", "value2"), IsStructNumber("number", 1.5),
                  IsStructBool("bool", true), IsStructNull("null", Protobuf::NULL_VALUE),
                  IsStructStruct(
                      "struct", UnorderedElementsAre(IsStructString("nested_key", "nested_value"))),
                  IsStructList("list", ElementsAre(IsStructValueString("list_string"),
                                                   IsStructValueNumber(2), IsStructValueBool(false),
                                                   IsStructValueNull(Protobuf::NULL_VALUE)))));

  EXPECT_THAT(value.fields().at("number"), IsStructValueNumber(1.5));
  EXPECT_THAT(value.fields().at("bool"), IsStructValueBool(true));
  EXPECT_THAT(value.fields().at("null"), IsStructValueNull(Protobuf::NULL_VALUE));
  EXPECT_THAT(value.fields().at("struct"), IsStructValueStruct(UnorderedElementsAre(
                                               IsStructString("nested_key", "nested_value"))));
  EXPECT_THAT(value.fields().at("list"),
              IsStructValueList(ElementsAre(IsStructValueString("list_string"),
                                            IsStructValueNumber(2), IsStructValueBool(false),
                                            IsStructValueNull(Protobuf::NULL_VALUE))));
}

TEST(StructMatchersTest, FailureDescriptionIncludesStructContents) {
  const auto value = makeTestStruct();

  const auto error =
      expectThatOutput(value.fields(), UnorderedElementsAre(IsStructString("key2", "value2")));
  // The output at time of writing looks like this:
  // ---
  // Expected: has 1 element and that element is struct string (key: "key2", expected: "value2")
  // Actual: { ("key1", <goo.gle/debugonly   string_value: "value1">), ("key2", <goo.gle/debugonly
  // string_value: "value2">) } which has 2 elements
  //
  // and where the following elements don't match any matchers:
  // element #0: ("key1", <goo.gle/debugonly   string_value: "value1">)
  // ---
  EXPECT_THAT(
      error, AllOf(HasSubstr("key1"), HasSubstr("value1"), HasSubstr("key2"), HasSubstr("value2")));
}

} // namespace Envoy
