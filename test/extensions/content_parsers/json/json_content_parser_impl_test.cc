#include "envoy/extensions/content_parsers/json/v3/json_content_parser.pb.h"

#include "source/extensions/content_parsers/json/json_content_parser_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ContentParsers {
namespace Json {
namespace {

using ProtoConfig = envoy::extensions::content_parsers::json::v3::JsonContentParser;

class JsonContentParserTest : public testing::Test {
public:
  void setupParser(const std::string& yaml) {
    TestUtility::loadFromYaml(yaml, proto_config_);
    parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);
  }

  const std::string basic_config_ = R"EOF(
rules:
  - rule:
      selectors:
        - key: "usage"
        - key: "total_tokens"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "tokens"
        type: NUMBER
  )EOF";

  ProtoConfig proto_config_; // Must outlive parser_ since Rule holds reference to it
  std::unique_ptr<JsonContentParserImpl> parser_;
};

TEST_F(JsonContentParserTest, BasicTokenExtraction) {
  setupParser(basic_config_);

  const std::string data =
      R"({"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);

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

  EXPECT_TRUE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0);
}

TEST_F(JsonContentParserTest, SelectorNotFound) {
  setupParser(basic_config_);

  const std::string data = R"({"other_field":"value"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0);
}

TEST_F(JsonContentParserTest, PartialSelectorPath) {
  setupParser(basic_config_);

  // Has 'usage' but not 'total_tokens'
  const std::string data = R"({"usage":{"prompt_tokens":10}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0);
}

TEST_F(JsonContentParserTest, DeepNestedPath) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "level1"
        - key: "level2"
        - key: "level3"
        - key: "value"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "deep_value"
  )EOF";
  setupParser(config);

  const std::string data = R"({"level1":{"level2":{"level3":{"value":99}}}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 99);
}

TEST_F(JsonContentParserTest, IntermediatePathNotObject) {
  setupParser(basic_config_);

  // 'usage' is a string, not an object, so can't traverse to 'total_tokens'
  const std::string data = R"({"usage":"not-an-object"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0);
}

TEST_F(JsonContentParserTest, NullValueInJson) {
  setupParser(R"EOF(
rules:
  - rule:
      selectors:
        - key: "usage"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "value"
  )EOF");

  const std::string data = R"({"usage":null})";
  auto result = parser_->parse(data);

  // Should fail to extract null value
  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0);
}

TEST_F(JsonContentParserTest, NestedObjectValue) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "usage"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "usage_obj"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"usage":{"tokens":30}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_TRUE(result.immediate_actions[0].value->has_string_value());
  EXPECT_NE(result.immediate_actions[0].value->string_value().find("tokens"), std::string::npos);
}

TEST_F(JsonContentParserTest, StringValueType) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "model"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "model_name"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"model":"gpt-4"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "gpt-4");
}

TEST_F(JsonContentParserTest, ProtobufValueType) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "value"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "test"
        type: PROTOBUF_VALUE
  )EOF";
  setupParser(config);

  const std::string data = R"({"value":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 42);
}

TEST_F(JsonContentParserTest, BooleanValue) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "enabled"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "flag"
  )EOF";
  setupParser(config);

  const std::string data = R"({"enabled":true})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->bool_value(), true);
}

TEST_F(JsonContentParserTest, StringToNumberConversionFailure) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "value"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "result"
        type: NUMBER
  )EOF";
  setupParser(config);

  // String value that cannot be converted to number
  const std::string data = R"({"value":"not-a-number"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  // Conversion fails, so value.kind_case() should be 0 (not set)
  EXPECT_EQ(result.immediate_actions[0].value->kind_case(), 0);
}

TEST_F(JsonContentParserTest, StringToNumberConversionSuccess) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "price"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "price_as_number"
        type: NUMBER
  )EOF";
  setupParser(config);

  // JSON field with string value that contains a valid number
  const std::string data = R"({"price":"123.45"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 123.45);
}

TEST_F(JsonContentParserTest, BoolToNumberConversion) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "flag"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "result"
        type: NUMBER
  )EOF";
  setupParser(config);

  const std::string data = R"({"flag":true})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 1.0);
}

TEST_F(JsonContentParserTest, BoolToStringConversion) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "flag"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "result"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"flag":false})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "false");
}

TEST_F(JsonContentParserTest, NumberToStringConversion) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "count"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "result"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"count":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "42");
}

TEST_F(JsonContentParserTest, DoubleToStringConversion) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "value"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "result"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"value":3.14})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].value->string_value(), "3.14");
}

TEST_F(JsonContentParserTest, IntegerValueExtraction) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "count"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "count_num"
        type: NUMBER
  - rule:
      selectors:
        - key: "count"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "count_str"
        type: STRING
  )EOF";
  setupParser(config);

  const std::string data = R"({"count":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 2);

  // Integer converted to number
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 42.0);

  // Integer converted to string
  EXPECT_EQ(result.immediate_actions[1].value->string_value(), "42");
}

TEST_F(JsonContentParserTest, OnMissing) {
  // Create config programmatically to properly set protobuf Value
  proto_config_.Clear();
  auto* rule = proto_config_.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("usage");
  rule->add_selectors()->set_key("total_tokens");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER);

  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("tokens");
  on_missing->mutable_value()->set_number_value(-1);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // Send JSON without "usage" field
  const std::string data = R"({"model": "gpt-4"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0);

  // Get all deferred actions at end of stream
  auto deferred = parser_->getAllDeferredActions();
  EXPECT_EQ(deferred.size(), 1);
  EXPECT_EQ(deferred[0].namespace_, "envoy.lb");
  EXPECT_EQ(deferred[0].key, "tokens");
  ASSERT_TRUE(deferred[0].value.has_value());
  EXPECT_EQ(deferred[0].value->number_value(), -1);
}

TEST_F(JsonContentParserTest, OnPresentWithHardcodedValue) {
  // Create config programmatically to properly set protobuf Value
  proto_config_.Clear();
  auto* rule = proto_config_.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("usage");
  rule->add_selectors()->set_key("total_tokens");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("tokens");
  on_present->mutable_value()->set_number_value(999);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // Extracted value is 42, but hardcoded value 999 should be used
  const std::string data = R"({"usage": {"total_tokens": 42}})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  ASSERT_TRUE(result.immediate_actions[0].value.has_value());
  EXPECT_EQ(result.immediate_actions[0].value->number_value(), 999); // Hardcoded value, not 42
}

TEST_F(JsonContentParserTest, StopProcessingAfterFirstMatch) {
  // Create config with stop_processing_after_matches = 1
  proto_config_.Clear();
  auto* rule_config = proto_config_.add_rules();
  rule_config->set_stop_processing_after_matches(1);

  auto* rule = rule_config->mutable_rule();
  rule->add_selectors()->set_key("model");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("model_name");
  on_present->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::STRING);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // First body - should match and extract
  const std::string data1 = R"({"model":"gpt-4"})";
  auto result1 = parser_->parse(data1);

  EXPECT_FALSE(result1.error_message.has_value());
  EXPECT_EQ(result1.immediate_actions.size(), 1);
  EXPECT_EQ(result1.immediate_actions[0].value->string_value(), "gpt-4");
  EXPECT_TRUE(result1.stop_processing); // Should stop after first match

  // Second body - should NOT process (rule already matched once)
  const std::string data2 = R"({"model":"gpt-3.5"})";
  auto result2 = parser_->parse(data2);

  EXPECT_FALSE(result2.error_message.has_value());
  EXPECT_EQ(result2.immediate_actions.size(), 0); // No action, rule skipped
  EXPECT_TRUE(result2.stop_processing);           // Still stopped
}

TEST_F(JsonContentParserTest, StopProcessingAfterMatchesDefault) {
  // Create config without setting stop_processing_after_matches (defaults to 0)
  proto_config_.Clear();
  auto* rule_config = proto_config_.add_rules();
  // NOT setting stop_processing_after_matches - should default to 0

  auto* rule = rule_config->mutable_rule();
  rule->add_selectors()->set_key("value");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("last_value");
  on_present->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // First body
  auto result1 = parser_->parse(R"({"value":10})");
  EXPECT_FALSE(result1.error_message.has_value());
  EXPECT_EQ(result1.immediate_actions.size(), 1);
  EXPECT_EQ(result1.immediate_actions[0].value->number_value(), 10);
  EXPECT_FALSE(result1.stop_processing); // Should NOT stop (default = 0)

  // Second body - should still process
  auto result2 = parser_->parse(R"({"value":20})");
  EXPECT_FALSE(result2.error_message.has_value());
  EXPECT_EQ(result2.immediate_actions.size(), 1);
  EXPECT_EQ(result2.immediate_actions[0].value->number_value(), 20); // Overwrites to 20
  EXPECT_FALSE(result2.stop_processing);

  // Third body - should still process (gets LAST value)
  auto result3 = parser_->parse(R"({"value":30})");
  EXPECT_FALSE(result3.error_message.has_value());
  EXPECT_EQ(result3.immediate_actions.size(), 1);
  EXPECT_EQ(result3.immediate_actions[0].value->number_value(), 30); // Gets last value
  EXPECT_FALSE(result3.stop_processing);
}

TEST_F(JsonContentParserTest, MultipleRulesWithDifferentStopBehavior) {
  // Create config with two rules:
  // Rule 1: stop after 1 match (extract model from first body)
  // Rule 2: default (0) - process all json bodies (extract tokens from last body)
  proto_config_.Clear();

  // Rule 1: Stop after first match
  auto* rule_config1 = proto_config_.add_rules();
  rule_config1->set_stop_processing_after_matches(1);
  auto* rule1 = rule_config1->mutable_rule();
  rule1->add_selectors()->set_key("model");
  auto* on_present1 = rule1->mutable_on_present();
  on_present1->set_metadata_namespace("envoy.lb");
  on_present1->set_key("model_name");
  on_present1->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::STRING);

  // Rule 2: Process all events (default stop_processing_after_matches = 0)
  auto* rule_config2 = proto_config_.add_rules();
  // NOT setting stop_processing_after_matches
  auto* rule2 = rule_config2->mutable_rule();
  rule2->add_selectors()->set_key("usage");
  rule2->add_selectors()->set_key("total_tokens");
  auto* on_present2 = rule2->mutable_on_present();
  on_present2->set_metadata_namespace("envoy.lb");
  on_present2->set_key("tokens");
  on_present2->set_type(
      envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::NUMBER);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // JSON body 1: Has model but no tokens
  auto result1 = parser_->parse(R"({"model":"gpt-4","id":"1"})");
  EXPECT_FALSE(result1.error_message.has_value());
  EXPECT_EQ(result1.immediate_actions.size(), 1); // Only rule 1 matched
  EXPECT_EQ(result1.immediate_actions[0].key, "model_name");
  EXPECT_EQ(result1.immediate_actions[0].value->string_value(), "gpt-4");
  EXPECT_FALSE(result1.stop_processing); // Rule 2 hasn't matched yet

  // JSON body 2: Has model again and partial tokens
  auto result2 = parser_->parse(R"({"model":"gpt-3.5","usage":{"total_tokens":10}})");
  EXPECT_FALSE(result2.error_message.has_value());
  EXPECT_EQ(result2.immediate_actions.size(), 1); // Only rule 2 (rule 1 already stopped)
  EXPECT_EQ(result2.immediate_actions[0].key, "tokens");
  EXPECT_EQ(result2.immediate_actions[0].value->number_value(), 10);
  EXPECT_FALSE(result2.stop_processing); // Rule 2 continues processing

  // JSON body 3: Final tokens (last JSON body)
  auto result3 = parser_->parse(R"({"usage":{"total_tokens":30}})");
  EXPECT_FALSE(result3.error_message.has_value());
  EXPECT_EQ(result3.immediate_actions.size(), 1);                    // Only rule 2
  EXPECT_EQ(result3.immediate_actions[0].value->number_value(), 30); // Gets last value
  EXPECT_FALSE(result3.stop_processing); // Rule 2 still doesn't stop (default = 0)
}

TEST_F(JsonContentParserTest, AllRulesStopAfterMatchCausesStreamStop) {
  // Create config where ALL rules have stop_processing_after_matches = 1
  proto_config_.Clear();

  auto* rule_config1 = proto_config_.add_rules();
  rule_config1->set_stop_processing_after_matches(1);
  auto* rule1 = rule_config1->mutable_rule();
  rule1->add_selectors()->set_key("model");
  auto* on_present1 = rule1->mutable_on_present();
  on_present1->set_metadata_namespace("envoy.lb");
  on_present1->set_key("model_name");

  auto* rule_config2 = proto_config_.add_rules();
  rule_config2->set_stop_processing_after_matches(1);
  auto* rule2 = rule_config2->mutable_rule();
  rule2->add_selectors()->set_key("id");
  auto* on_present2 = rule2->mutable_on_present();
  on_present2->set_metadata_namespace("envoy.lb");
  on_present2->set_key("request_id");

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // JSON body 1: Only rule 1 matches
  auto result1 = parser_->parse(R"({"model":"gpt-4"})");
  EXPECT_EQ(result1.immediate_actions.size(), 1);
  EXPECT_FALSE(result1.stop_processing); // Rule 2 hasn't matched yet

  // JSON body 2: Only rule 2 matches - NOW both rules have matched
  auto result2 = parser_->parse(R"({"id":"req-123"})");
  EXPECT_EQ(result2.immediate_actions.size(), 1);
  EXPECT_TRUE(result2.stop_processing); // ALL rules with stop > 0 have matched!

  // JSON body 3: Should not process any rules
  auto result3 = parser_->parse(R"({"model":"gpt-3.5","id":"req-456"})");
  EXPECT_EQ(result3.immediate_actions.size(), 0); // Both rules already stopped
  EXPECT_TRUE(result3.stop_processing);
}

TEST_F(JsonContentParserTest, OnErrorDeferredAction) {
  // Create config programmatically to properly set protobuf Value
  proto_config_.Clear();
  auto* rule = proto_config_.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("usage");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("usage");

  auto* on_error = rule->mutable_on_error();
  on_error->set_metadata_namespace("envoy.errors");
  on_error->set_key("parse_error");
  on_error->mutable_value()->set_string_value("failed");

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // Parse invalid JSON to trigger error state
  const std::string invalid_data = "[DONE]";
  auto result = parser_->parse(invalid_data);
  EXPECT_TRUE(result.error_message.has_value());

  // Get all deferred actions at end of stream - should return on_error action
  auto deferred = parser_->getAllDeferredActions();
  EXPECT_EQ(deferred.size(), 1);
  EXPECT_EQ(deferred[0].namespace_, "envoy.errors");
  EXPECT_EQ(deferred[0].key, "parse_error");
  ASSERT_TRUE(deferred[0].value.has_value());
  EXPECT_EQ(deferred[0].value->string_value(), "failed");
}

TEST_F(JsonContentParserTest, DoubleValueExtraction) {
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "price"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "price"
        type: NUMBER
  - rule:
      selectors:
        - key: "score"
      on_present:
        metadata_namespace: "envoy.lb"
        key: "score"
        type: PROTOBUF_VALUE
  )EOF";
  setupParser(config);

  const std::string data = R"({"price":19.99,"score":3.14})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 2);

  // First action: NUMBER type with double
  EXPECT_EQ(result.immediate_actions[0].namespace_, "envoy.lb");
  EXPECT_EQ(result.immediate_actions[0].key, "price");
  ASSERT_TRUE(result.immediate_actions[0].value.has_value());
  EXPECT_DOUBLE_EQ(result.immediate_actions[0].value->number_value(), 19.99);

  // Second action: PROTOBUF_VALUE type with double
  EXPECT_EQ(result.immediate_actions[1].namespace_, "envoy.lb");
  EXPECT_EQ(result.immediate_actions[1].key, "score");
  ASSERT_TRUE(result.immediate_actions[1].value.has_value());
  EXPECT_DOUBLE_EQ(result.immediate_actions[1].value->number_value(), 3.14);
}

TEST_F(JsonContentParserTest, DefaultNamespaceWhenEmpty) {
  // Config with empty metadata_namespace - should use default
  const std::string config = R"EOF(
rules:
  - rule:
      selectors:
        - key: "value"
      on_present:
        key: "result"
  )EOF";
  setupParser(config);

  const std::string data = R"({"value":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_EQ(result.immediate_actions[0].namespace_, "envoy.content_parsers.json");
  EXPECT_EQ(result.immediate_actions[0].key, "result");
}

TEST_F(JsonContentParserTest, PreserveExistingMetadataValue) {
  proto_config_.Clear();
  auto* rule = proto_config_.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("value");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("result");
  on_present->set_preserve_existing_metadata_value(true);

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  const std::string data = R"({"value":42})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
  EXPECT_TRUE(result.immediate_actions[0].preserve_existing);
}

TEST_F(JsonContentParserTest, FactoryCreateParser) {
  setupParser(basic_config_);
  JsonContentParserFactory factory(proto_config_);

  auto parser = factory.createParser();
  EXPECT_NE(parser, nullptr);

  // Verify the created parser works
  const std::string data =
      R"({"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}})";
  auto result = parser->parse(data);
  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 1);
}

TEST_F(JsonContentParserTest, FactoryStatsPrefix) {
  setupParser(basic_config_);
  JsonContentParserFactory factory(proto_config_);

  EXPECT_EQ(factory.statsPrefix(), "json.");
}

TEST_F(JsonContentParserTest, OnErrorPriorityOverOnMissing) {
  // Rule with both on_error and on_missing configured
  proto_config_.Clear();
  auto* rule = proto_config_.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("value");

  auto* on_present = rule->mutable_on_present();
  on_present->set_metadata_namespace("envoy.lb");
  on_present->set_key("result");

  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("missing_fallback");
  on_missing->mutable_value()->set_string_value("was_missing");

  auto* on_error = rule->mutable_on_error();
  on_error->set_metadata_namespace("envoy.lb");
  on_error->set_key("error_fallback");
  on_error->mutable_value()->set_string_value("had_error");

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // Parse invalid JSON - should trigger on_error, NOT on_missing
  const std::string invalid_data = "[DONE]";
  auto result = parser_->parse(invalid_data);
  EXPECT_TRUE(result.error_message.has_value());

  auto deferred = parser_->getAllDeferredActions();
  EXPECT_EQ(deferred.size(), 1);
  EXPECT_EQ(deferred[0].key, "error_fallback"); // on_error takes priority
  EXPECT_EQ(deferred[0].value->string_value(), "had_error");
}

TEST_F(JsonContentParserTest, RuleWithOnlyOnMissing) {
  // Rule with only on_missing, no on_present
  proto_config_.Clear();
  auto* rule = proto_config_.add_rules()->mutable_rule();
  rule->add_selectors()->set_key("nonexistent");

  auto* on_missing = rule->mutable_on_missing();
  on_missing->set_metadata_namespace("envoy.lb");
  on_missing->set_key("fallback");
  on_missing->mutable_value()->set_string_value("default_value");

  parser_ = std::make_unique<JsonContentParserImpl>(proto_config_);

  // Parse valid JSON but selector not found
  const std::string data = R"({"other_field":"value"})";
  auto result = parser_->parse(data);

  EXPECT_FALSE(result.error_message.has_value());
  EXPECT_EQ(result.immediate_actions.size(), 0); // No on_present configured

  auto deferred = parser_->getAllDeferredActions();
  EXPECT_EQ(deferred.size(), 1);
  EXPECT_EQ(deferred[0].key, "fallback");
  EXPECT_EQ(deferred[0].value->string_value(), "default_value");
}

} // namespace
} // namespace Json
} // namespace ContentParsers
} // namespace Extensions
} // namespace Envoy
