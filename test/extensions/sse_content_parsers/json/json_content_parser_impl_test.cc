#include "envoy/extensions/sse_content_parsers/json/v3/json_content_parser.pb.h"

#include "source/extensions/sse_content_parsers/json/json_content_parser_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace SseContentParsers {
namespace Json {
namespace {

using ProtoConfig = envoy::extensions::sse_content_parsers::json::v3::JsonContentParser;

class JsonContentParserTest : public testing::Test {
public:
  void setupParser(const std::string& yaml) {
    ProtoConfig proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    parser_ = std::make_unique<JsonContentParserImpl>(proto_config);
  }

  const std::string basic_config_ = R"EOF(
rules:
  - selector:
      path: ["usage", "total_tokens"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "tokens"
        type: NUMBER
  )EOF";

  std::unique_ptr<JsonContentParserImpl> parser_;
};

TEST_F(JsonContentParserTest, BasicTokenExtraction) {
  setupParser(basic_config_);

  const std::string data =
      R"({"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.matched_rules.size(), 1);
  EXPECT_EQ(result.selector_not_found_rules.size(), 0);

  const auto& action = result.immediate_actions[0];
  EXPECT_EQ(action.namespace_, "envoy.lb");
  EXPECT_EQ(action.key, "tokens");
  ASSERT_TRUE(action.value.has_value());
  EXPECT_EQ(action.value->number_value(), 30);
}

TEST_F(JsonContentParserTest, InvalidJson) {
  setupParser(basic_config_);

  const std::string data = "[DONE]";
  auto result = parser_->parse(data);

  EXPECT_TRUE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 0);
}

TEST_F(JsonContentParserTest, SelectorNotFound) {
  setupParser(basic_config_);

  const std::string data = R"({"other_field":"value"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 0);
  EXPECT_EQ(result.selector_not_found_rules.size(), 1);
  EXPECT_EQ(result.selector_not_found_rules[0], 0); // Rule 0 didn't find selector
}

TEST_F(JsonContentParserTest, PartialSelectorPath) {
  setupParser(basic_config_);

  // Has 'usage' but not 'total_tokens'
  const std::string data = R"({"usage":{"prompt_tokens":10}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 0);
  EXPECT_EQ(result.selector_not_found_rules.size(), 1);
}

TEST_F(JsonContentParserTest, DeepNestedPath) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["level1", "level2", "level3", "value"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "deep_value"
  )EOF";
  setupParser(config);

  const std::string data = R"({"level1":{"level2":{"level3":{"value":99}}}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 99);
}

TEST_F(JsonContentParserTest, IntermediatePathNotObject) {
  setupParser(basic_config_);

  // 'usage' is a string, not an object, so can't traverse to 'total_tokens'
  const std::string data = R"({"usage":"not-an-object"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 0);
  EXPECT_EQ(result.selector_not_found_rules.size(), 1);
}

TEST_F(JsonContentParserTest, NullValueInJson) {
  setupParser(R"EOF(
rules:
  - selector:
      path: ["usage"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "value"
  )EOF");

  const std::string data = R"({"usage":null})";
  auto result = parser_->parse(data);

  // Should fail to extract null value
  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 0);
  EXPECT_EQ(result.selector_not_found_rules.size(), 1);
}

TEST_F(JsonContentParserTest, NestedObjectValue) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["usage"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "usage_obj"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"usage":{"tokens":30}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_TRUE(result.immediate_actions[0].value->has_string_value());
  EXPECT_NE(result.immediate_actions[0].value->string_value().find("tokens"), std::string::npos);
}

TEST_F(JsonContentParserTest, StringValueType) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["model"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "model_name"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"model":"gpt-4"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "gpt-4");
}

TEST_F(JsonContentParserTest, ProtobufValueType) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["value"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "test"
        type: PROTOBUF_VALUE
  )EOF";
  setupParser(config);

  const std::string data = R"({"value":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 42);
}

TEST_F(JsonContentParserTest, BooleanValue) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["enabled"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "flag"
  )EOF";
  setupParser(config);

  const std::string data = R"({"enabled":true})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->bool_value(), true);
}

TEST_F(JsonContentParserTest, StringToNumberConversionFailure) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["value"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "result"
        type: NUMBER
  )EOF";
  setupParser(config);

  // String value that cannot be converted to number
  const std::string data = R"({"value":"not-a-number"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  // Conversion fails, so value.kind_case() should be 0 (not set)
  EXPECT_EQ(result.immediate_actions[0].value->kind_case(), 0);
}

TEST_F(JsonContentParserTest, StringToNumberConversionSuccess) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["price"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "price_as_number"
        type: NUMBER
  )EOF";
  setupParser(config);

  // JSON field with string value that contains a valid number
  const std::string data = R"({"price":"123.45"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 123.45);
}

TEST_F(JsonContentParserTest, BoolToNumberConversion) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["flag"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "result"
        type: NUMBER
  )EOF";
  setupParser(config);

  const std::string data = R"({"flag":true})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 1.0);
}

TEST_F(JsonContentParserTest, BoolToStringConversion) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["flag"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "result"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"flag":false})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "false");
}

TEST_F(JsonContentParserTest, NumberToStringConversion) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["count"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "result"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"count":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "42");
}

TEST_F(JsonContentParserTest, DoubleToStringConversion) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["value"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "result"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"value":3.14})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "3.14");
}

TEST_F(JsonContentParserTest, IntegerValueExtraction) {
  const std::string config = R"EOF(
rules:
  - selector:
      path: ["count"]
    on_present:
      - metadata_namespace: "envoy.lb"
        key: "count_num"
        type: NUMBER
      - metadata_namespace: "envoy.lb"
        key: "count_str"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"count":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 2);

  // Integer converted to number
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 42.0);

  // Integer converted to string
  EXPECT_EQ(result.immediate_actions[1].value->string_value(), "42");
}

TEST_F(JsonContentParserTest, OnMissing) {
  // Create config programmatically to properly set protobuf Value
  ProtoConfig proto_config;
  auto* rule = proto_config.add_rules();
  rule->mutable_selector()->add_path("usage");
  rule->mutable_selector()->add_path("total_tokens");

  auto* on_present = rule->add_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->set_type(envoy::extensions::sse_content_parsers::json::v3::JsonContentParser::NUMBER);

  auto* on_missing = rule->add_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");
  on_missing->mutable_value()->set_number_value(-1);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config);

  // Send JSON without "usage" field
  const std::string data = R"({"model": "gpt-4"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 0);
  EXPECT_EQ(result.selector_not_found_rules.size(), 1);

  // Get deferred actions
  auto deferred = parser_->getDeferredActions(0, false, true);
  EXPECT_EQ(deferred.size(), 1);
  EXPECT_EQ(deferred[0].namespace_, "envoy.lb");
  EXPECT_EQ(deferred[0].key, "tokens");
  ASSERT_TRUE(deferred[0].value.has_value());
  EXPECT_EQ(deferred[0].value->number_value(), -1);
}

TEST_F(JsonContentParserTest, OnPresentWithHardcodedValue) {
  // Create config programmatically to properly set protobuf Value
  ProtoConfig proto_config;
  auto* rule = proto_config.add_rules();
  rule->mutable_selector()->add_path("usage");
  rule->mutable_selector()->add_path("total_tokens");

  auto* on_present = rule->add_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->mutable_value()->set_number_value(999);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config);

  // Extracted value is 42, but hardcoded value 999 should be used
  const std::string data = R"({"usage": {"total_tokens": 42}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.has_error);
  EXPECT_EQ(result.immediate_actions.size(), 1);
  ASSERT_TRUE(result.immediate_actions[0].value.has_value());
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 999); // Hardcoded value, not 42
}

} // namespace
} // namespace Json
} // namespace SseContentParsers
} // namespace Extensions
} // namespace Envoy
