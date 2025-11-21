#include <string>

#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

class McpJsonParserTest : public testing::Test {
protected:
  void SetUp() override {
    config_ = McpParserConfig::createDefault();
    parser_ = std::make_unique<McpJsonParser>(config_);
  }

  void parseJson(const std::string& json) {
    auto status = parser_->parse(json);
    EXPECT_OK(status);
  }

  McpParserConfig config_;
  std::unique_ptr<McpJsonParser> parser_;
};

TEST_F(McpJsonParserTest, ValidJsonRpcRequest) {
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "id": 1})";

  EXPECT_OK(parser_->parse(json));
  ASSERT_TRUE(parser_->finishParse().ok());

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), "test");
}

TEST_F(McpJsonParserTest, MissingJsonRpcVersion) {
  std::string json = R"({"method": "test", "id": 1})";

  EXPECT_OK(parser_->parse(json));
  EXPECT_FALSE(parser_->isValidMcpRequest());
}

TEST_F(McpJsonParserTest, InvalidJsonRpcVersion) {
  std::string json = R"({"jsonrpc": "1.0", "method": "test", "id": 1})";

  EXPECT_OK(parser_->parse(json));

  EXPECT_FALSE(parser_->isValidMcpRequest());
}

TEST_F(McpJsonParserTest, MissingMethod) {
  std::string json = R"({"jsonrpc": "2.0", "id": 1})";

  EXPECT_OK(parser_->parse(json));

  EXPECT_FALSE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), "");
}

TEST_F(McpJsonParserTest, ToolsCallExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "calculator",
      "arguments": {"x": 10, "y": 20}
    },
    "id": 123
  })";

  EXPECT_OK(parser_->parse(json));

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  // Check extracted metadata contains params.name
  const auto* value = parser_->getNestedValue("params.name");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "calculator");
}

TEST_F(McpJsonParserTest, ResourcesReadExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "resources/read",
    "params": {
      "uri": "file:///path/to/resource.txt"
    },
    "id": "request-456"
  })";

  EXPECT_OK(parser_->parse(json));

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_READ);

  const auto* value = parser_->getNestedValue("params.uri");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "file:///path/to/resource.txt");
}

TEST_F(McpJsonParserTest, PromptsGetExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "prompts/get",
    "params": {
      "name": "greeting_prompt",
      "arguments": {
        "language": "en",
        "style": "formal"
      }
    },
    "id": 789
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::PROMPTS_GET);

  const auto* value = parser_->getNestedValue("params.name");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "greeting_prompt");
}

TEST_F(McpJsonParserTest, CompletionCompleteExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "completion/complete",
    "params": {
      "ref": {
        "type": "ref/resource",
        "uri": "file:///document.md"
      },
      "argument": {
        "name": "prefix",
        "value": "def "
      }
    },
    "id": 1001
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::COMPLETION_COMPLETE);
}

TEST_F(McpJsonParserTest, InitializeExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "initialize",
    "params": {
      "protocolVersion": "1.0",
      "capabilities": {
        "tools": {},
        "resources": {
          "subscribe": true
        },
        "prompts": {}
      },
      "clientInfo": {
        "name": "test-client",
        "version": "0.1.0"
      }
    },
    "id": "init-1"
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::INITIALIZE);

  const auto* version_value = parser_->getNestedValue("params.protocolVersion");
  ASSERT_NE(version_value, nullptr);
  EXPECT_EQ(version_value->string_value(), "1.0");

  const auto* client_name = parser_->getNestedValue("params.clientInfo.name");
  ASSERT_NE(client_name, nullptr);
  EXPECT_EQ(client_name->string_value(), "test-client");
}

TEST_F(McpJsonParserTest, NotificationProgressExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "notifications/progress",
    "params": {
      "progressToken": "task-123",
      "progress": 75
    }
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::NOTIFICATION_PROGRESS);

  const auto* token = parser_->getNestedValue("params.progressToken");
  ASSERT_NE(token, nullptr);
  EXPECT_EQ(token->string_value(), "task-123");

  const auto* progress = parser_->getNestedValue("params.progress");
  ASSERT_NE(progress, nullptr);
  EXPECT_EQ(progress->number_value(), 75);
}

TEST_F(McpJsonParserTest, NotificationCancelledExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "notifications/cancelled",
    "params": {
      "requestId": "req-999",
      "reason": "user_cancelled"
    }
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::NOTIFICATION_CANCELLED);

  const auto* request_id = parser_->getNestedValue("params.requestId");
  ASSERT_NE(request_id, nullptr);
  EXPECT_EQ(request_id->string_value(), "req-999");
}

TEST_F(McpJsonParserTest, GenericNotification) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "notifications/custom_event",
    "params": {
      "data": "some_data"
    }
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), "notifications/custom_event");
}

TEST_F(McpJsonParserTest, PartialParsingSingleChunk) {
  std::string json1 = R"({"jsonrpc": "2.0", "me)";
  std::string json2 = R"(thod": "tools/call", "params": {"na)";
  std::string json3 = R"(me": "test"}, "id": 1})";

  auto status1 = parser_->parse(json1);
  EXPECT_OK(status1); // Incomplete JSON

  auto status2 = parser_->parse(json2);
  EXPECT_OK(status2); // Still incomplete

  auto status3 = parser_->parse(json3);
  EXPECT_OK(status3); // Complete now

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  const auto* value = parser_->getNestedValue("params.name");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "test");
}

TEST_F(McpJsonParserTest, PartialParsingMidString) {
  // Split in the middle of a string value
  std::string json1 =
      R"({"jsonrpc": "2.0", "method": "resources/read", "params": {"uri": "file:///very/long/)";
  std::string json2 = R"(path/to/some/resource/file.txt"}, "id": 42})";

  auto status1 = parser_->parse(json1);
  EXPECT_OK(status1);

  auto status2 = parser_->parse(json2);
  EXPECT_OK(status2);

  ASSERT_TRUE(parser_->finishParse().ok());

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_READ);

  const auto* value = parser_->getNestedValue("params.uri");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "file:///very/long/path/to/some/resource/file.txt");
}

TEST_F(McpJsonParserTest, PartialParsingEscapeSequence) {
  // Split in the middle of an escape sequence
  std::string json1 = R"({"jsonrpc": "2.0", "method": "test", "params": {"text": "line1\)";

  auto status1 = parser_->parse(json1);

  // Early termination.
  EXPECT_FALSE(status1.ok());

  EXPECT_TRUE(parser_->isValidMcpRequest());
}

TEST_F(McpJsonParserTest, EarlyTerminationToolsCall) {
  // Large JSON that should stop parsing after finding all required fields
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "calculator",
      "arguments": {
        "operation": "add",
        "x": 100,
        "y": 200
      }
    },
    "id": 1,
    "extra_field_1": "this is extra data that should not be parsed",
    "extra_field_2": {"nested": {"deeply": {"more": "data"}}},
    "extra_field_3": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  // Should have extracted params.name
  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "calculator");

  // Should not have extracted extra fields
  const auto* extra = parser_->getNestedValue("extra_field_1");
  EXPECT_EQ(extra, nullptr);
}

TEST_F(McpJsonParserTest, EarlyTerminationWithUnorderedFields) {
  // Method comes after params - parser should handle this correctly
  std::string json = R"({
    "id": 123,
    "params": {
      "uri": "file:///test.txt",
      "extra_param": "should_be_stored"
    },
    "jsonrpc": "2.0",
    "method": "resources/read",
    "extra_field": "should_not_be_parsed"
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_READ);

  // Should have extracted params.uri
  const auto* uri = parser_->getNestedValue("params.uri");
  ASSERT_NE(uri, nullptr);
  EXPECT_EQ(uri->string_value(), "file:///test.txt");
}

TEST_F(McpJsonParserTest, DeeplyNestedStructures) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "completion/complete",
    "params": {
      "ref": {
        "type": "ref/resource",
        "uri": "file:///doc.md",
        "metadata": {
          "author": "test",
          "tags": ["tag1", "tag2"]
        }
      },
      "argument": {
        "name": "prefix",
        "value": "function"
      }
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::COMPLETION_COMPLETE);
}

TEST_F(McpJsonParserTest, InvalidJson) {
  std::string json = R"({"invalid json": "a})";

  auto status = parser_->parse(json);
  EXPECT_OK(status);
  auto finish_status = parser_->finishParse();
  EXPECT_FALSE(finish_status.ok());
  EXPECT_THAT(finish_status.message(), HasSubstr("Closing quote expected in string"));
}

TEST_F(McpJsonParserTest, EmptyJson) {
  std::string json = "";

  auto status = parser_->parse(json);
  EXPECT_TRUE(status.ok()); // Parser accepts empty input

  auto finish_status = parser_->finishParse();
  EXPECT_FALSE(finish_status.ok());
  EXPECT_THAT(finish_status.message(), HasSubstr("Unexpected end of string"));
}

TEST_F(McpJsonParserTest, NullValues) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "prompts/get",
    "params": {
      "name": null,
      "value": null
    },
    "id": null
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());

  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_TRUE(name->has_null_value());
}

TEST_F(McpJsonParserTest, NumericValues) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "logging/setLevel",
    "params": {
      "level": 3
    },
    "id": 999
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::LOGGING_SET_LEVEL);

  const auto* level = parser_->getNestedValue("params.level");
  ASSERT_NE(level, nullptr);
  EXPECT_EQ(level->number_value(), 3);
}

TEST_F(McpJsonParserTest, ArraysAreSkipped) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "test",
    "params": {
      "name": "test",
      "items": [1, 2, 3, 4, 5]
    },
    "id": 1
  })";

  parseJson(json);
  EXPECT_TRUE(parser_->isValidMcpRequest());
}

TEST_F(McpJsonParserTest, ResetAndReuse) {
  // First parse
  std::string json1 =
      R"({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "tool1"}, "id": 1})";
  parseJson(json1);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  const auto* name1 = parser_->getNestedValue("params.name");
  ASSERT_NE(name1, nullptr);
  EXPECT_EQ(name1->string_value(), "tool1");

  // Reset and parse different JSON
  parser_->reset();

  std::string json2 =
      R"({"jsonrpc": "2.0", "method": "resources/read", "params": {"uri": "file:///test.txt"}, "id": 2})";
  parseJson(json2);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_READ);

  const auto* uri = parser_->getNestedValue("params.uri");
  ASSERT_NE(uri, nullptr);
  EXPECT_EQ(uri->string_value(), "file:///test.txt");

  // Old field should not exist
  const auto* old_name = parser_->getNestedValue("params.name");
  EXPECT_EQ(old_name, nullptr);
}

TEST_F(McpJsonParserTest, CustomFieldExtraction) {
  // Create custom config that extracts additional fields
  McpParserConfig custom_config;
  std::vector<McpParserConfig::FieldRule> rules = {
      McpParserConfig::FieldRule("params.name"), McpParserConfig::FieldRule("params.custom_field"),
      McpParserConfig::FieldRule("params.metadata.version")};

  custom_config.addMethodConfig(McpConstants::Methods::TOOLS_CALL, rules);

  auto custom_parser = std::make_unique<McpJsonParser>(custom_config);

  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "custom_tool",
      "custom_field": "custom_value",
      "metadata": {
        "version": "1.2.3",
        "author": "test"
      }
    },
    "id": 1
  })";

  EXPECT_OK(custom_parser->parse(json));

  EXPECT_TRUE(custom_parser->isValidMcpRequest());

  // Check all custom fields were extracted
  const auto* name = custom_parser->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "custom_tool");

  const auto* custom_field = custom_parser->getNestedValue("params.custom_field");
  ASSERT_NE(custom_field, nullptr);
  EXPECT_EQ(custom_field->string_value(), "custom_value");

  const auto* version = custom_parser->getNestedValue("params.metadata.version");
  ASSERT_NE(version, nullptr);
  EXPECT_EQ(version->string_value(), "1.2.3");

  // Author should not be extracted (not in config)
  const auto* author = custom_parser->getNestedValue("params.metadata.author");
  EXPECT_EQ(author, nullptr);
}

TEST_F(McpJsonParserTest, BooleanValues) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "resources/subscribe",
    "params": {
      "uri": "file:///test",
      "subscribe": true
    },
    "id": 1
  })";

  // Create custom config that extracts additional fields
  McpParserConfig custom_config;
  std::vector<McpParserConfig::FieldRule> rules = {
      McpParserConfig::FieldRule("params.subscribe"),
  };

  custom_config.addMethodConfig(McpConstants::Methods::RESOURCES_SUBSCRIBE, rules);

  auto parser = std::make_unique<McpJsonParser>(custom_config);

  EXPECT_OK(parser->parse(json));

  EXPECT_TRUE(parser->isValidMcpRequest());
  EXPECT_EQ(parser->getMethod(), McpConstants::Methods::RESOURCES_SUBSCRIBE);

  const auto* subscribe = parser->getNestedValue("params.subscribe");
  ASSERT_NE(subscribe, nullptr);
  EXPECT_TRUE(subscribe->bool_value());
}

TEST_F(McpJsonParserTest, LargePayload) {
  // Create a large JSON payload
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "large_tool",)";

  // Add many fields that should be ignored
  for (int i = 0; i < 100; ++i) {
    json += absl::StrCat(R"("field_)", i, R"(": "value_)", i, R"(",)");
  }

  json += R"("last_field": "last_value"
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  // Should have only extracted params.name
  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "large_tool");

  // Random fields should not be extracted
  const auto* field_50 = parser_->getNestedValue("params.field_50");
  EXPECT_EQ(field_50, nullptr);
}

TEST_F(McpJsonParserTest, UnicodeCharacters) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "测试工具",
      "description": "Test with émojis 🚀 and symbols ♠♣♥♦"
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());

  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "测试工具");
}

TEST_F(McpJsonParserTest, SpecialCharactersInStrings) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "resources/read",
    "params": {
      "uri": "file:///path/with spaces/and-special@chars#test.txt"
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());

  const auto* uri = parser_->getNestedValue("params.uri");
  ASSERT_NE(uri, nullptr);
  EXPECT_EQ(uri->string_value(), "file:///path/with spaces/and-special@chars#test.txt");
}

TEST_F(McpJsonParserTest, Uint64Values) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "logging/setLevel",
    "params": {
      "level": 18446744073709551615
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());

  const auto* level = parser_->getNestedValue("params.level");
  ASSERT_NE(level, nullptr);
  // Protobuf Value stores numbers as doubles, so precision might be lost for very large uint64
  // but we just want to verify RenderUint64 path is taken and value is stored.
  EXPECT_EQ(level->number_value(), static_cast<double>(18446744073709551615ULL));
}

TEST_F(McpJsonParserTest, NestedArraySkipping) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tool1",
      "args": [
        {"nested": "value"},
        ["more", "nested"]
      ]
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "tool1");
}

TEST_F(McpJsonParserTest, GetNestedValueIntermediateNonStruct) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "tool1"
    },
    "id": 1
  })";

  parseJson(json);

  // Try to access a child of a string value
  const auto* value = parser_->getNestedValue("params.name.child");
  EXPECT_EQ(value, nullptr);
}

TEST_F(McpJsonParserTest, CopyFieldCollision) {
  // Config tries to extract both "params.a" and "params.a.b"
  // This is a configuration error effectively, but parser should handle it gracefully
  McpParserConfig custom_config;
  std::vector<McpParserConfig::FieldRule> rules = {McpParserConfig::FieldRule("params.a"),
                                                   McpParserConfig::FieldRule("params.a.b")};
  custom_config.addMethodConfig("test", rules);

  auto parser = std::make_unique<McpJsonParser>(custom_config);

  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "test",
    "params": {
      "a": "string_value"
    },
    "id": 1
  })";

  EXPECT_OK(parser->parse(json));

  // params.a should be extracted
  const auto* a = parser->getNestedValue("params.a");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->string_value(), "string_value");

  // params.a.b cannot be extracted because params.a is a string, not a struct
  const auto* b = parser->getNestedValue("params.a.b");
  EXPECT_EQ(b, nullptr);
}

TEST_F(McpJsonParserTest, FromProtoConfig) {
  envoy::extensions::filters::http::mcp::v3::ParserConfig proto_config;
  auto* method_rule = proto_config.add_methods();
  method_rule->set_method("custom/method");
  method_rule->add_fields()->set_path("params.field1");
  method_rule->add_fields()->set_path("params.field2");

  McpParserConfig config = McpParserConfig::fromProto(proto_config);

  const auto& fields = config.getFieldsForMethod("custom/method");
  ASSERT_EQ(fields.size(), 2);
  EXPECT_EQ(fields[0].path, "params.field1");
  EXPECT_EQ(fields[1].path, "params.field2");

  // Default fields should still be there (implicit in implementation)
  EXPECT_TRUE(config.getAlwaysExtract().contains("jsonrpc"));
}

TEST_F(McpJsonParserTest, FloatingPointValues) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "logging/setLevel",
    "params": {
      "level": 3.14159,
      "other": 1.5
    },
    "id": 1
  })";

  // We need to configure the parser to extract these fields
  McpParserConfig custom_config;
  std::vector<McpParserConfig::FieldRule> rules = {McpParserConfig::FieldRule("params.level"),
                                                   McpParserConfig::FieldRule("params.other")};
  custom_config.addMethodConfig(McpConstants::Methods::LOGGING_SET_LEVEL, rules);

  auto parser = std::make_unique<McpJsonParser>(custom_config);

  EXPECT_OK(parser->parse(json));

  EXPECT_TRUE(parser->isValidMcpRequest());

  const auto* level = parser->getNestedValue("params.level");
  ASSERT_NE(level, nullptr);
  EXPECT_DOUBLE_EQ(level->number_value(), 3.14159);

  const auto* other = parser->getNestedValue("params.other");
  ASSERT_NE(other, nullptr);
  EXPECT_DOUBLE_EQ(other->number_value(), 1.5);
}

TEST(McpFieldExtractorTest, DirectIntegerRendering) {
  McpParserConfig config;
  std::vector<McpParserConfig::FieldRule> rules = {McpParserConfig::FieldRule("int32_val"),
                                                   McpParserConfig::FieldRule("uint32_val"),
                                                   McpParserConfig::FieldRule("int64_val")};
  config.addMethodConfig("test_method", rules);

  Protobuf::Struct metadata;
  McpFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "test_method");

  // Test RenderInt32
  extractor.RenderInt32("int32_val", -123);

  // Test RenderUint32
  extractor.RenderUint32("uint32_val", 456);

  // Test RenderInt64
  extractor.RenderInt64("int64_val", -789);

  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidMcp());
  EXPECT_EQ(extractor.getMethod(), "test_method");

  // Verify results
  const auto& fields = metadata.fields();

  ASSERT_TRUE(fields.contains("int32_val"));
  EXPECT_EQ(fields.at("int32_val").number_value(), -123.0);

  ASSERT_TRUE(fields.contains("uint32_val"));
  EXPECT_EQ(fields.at("uint32_val").number_value(), 456.0);

  ASSERT_TRUE(fields.contains("int64_val"));
  EXPECT_EQ(fields.at("int64_val").number_value(), -789.0);
}

TEST_F(McpJsonParserTest, FinishParseWithoutParsing) {
  // Test calling finishParse() without calling parse() first
  auto status = parser_->finishParse();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("No data has been parsed"));
}

TEST(McpFieldExtractorTest, RenderBytesAndFloat) {
  McpParserConfig config;
  std::vector<McpParserConfig::FieldRule> rules = {McpParserConfig::FieldRule("bytes_val"),
                                                   McpParserConfig::FieldRule("float_val")};
  config.addMethodConfig("test_method", rules);

  Protobuf::Struct metadata;
  McpFieldExtractor extractor(metadata, config);

  extractor.StartObject("");
  extractor.RenderString("jsonrpc", "2.0");
  extractor.RenderString("method", "test_method");

  // Test RenderBytes - should behave like RenderString
  extractor.RenderBytes("bytes_val", "binary_data");

  // Test RenderFloat - should behave like RenderDouble
  extractor.RenderFloat("float_val", 3.14f);

  extractor.EndObject();
  extractor.finalizeExtraction();

  EXPECT_TRUE(extractor.isValidMcp());
  EXPECT_EQ(extractor.getMethod(), "test_method");

  // Verify results
  const auto& fields = metadata.fields();

  ASSERT_TRUE(fields.contains("bytes_val"));
  EXPECT_EQ(fields.at("bytes_val").string_value(), "binary_data");

  ASSERT_TRUE(fields.contains("float_val"));
  EXPECT_FLOAT_EQ(fields.at("float_val").number_value(), 3.14);
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
