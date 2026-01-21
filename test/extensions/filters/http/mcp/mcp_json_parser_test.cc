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

TEST_F(McpJsonParserTest, ResourcesListExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "resources/list",
    "id": 100
  })";

  EXPECT_OK(parser_->parse(json));

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_LIST);
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

TEST_F(McpJsonParserTest, ResourcesSubscribeExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "resources/subscribe",
    "params": {
      "uri": "file:///config/settings.json"
    },
    "id": 102
  })";

  EXPECT_OK(parser_->parse(json));

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_SUBSCRIBE);

  const auto* value = parser_->getNestedValue("params.uri");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "file:///config/settings.json");
}

TEST_F(McpJsonParserTest, ResourcesUnsubscribeExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "resources/unsubscribe",
    "params": {
      "uri": "file:///config/settings.json"
    },
    "id": 103
  })";

  EXPECT_OK(parser_->parse(json));

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::RESOURCES_UNSUBSCRIBE);

  const auto* value = parser_->getNestedValue("params.uri");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->string_value(), "file:///config/settings.json");
}

TEST_F(McpJsonParserTest, PromptsListExtraction) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "prompts/list",
    "id": 200
  })";

  EXPECT_OK(parser_->parse(json));

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::PROMPTS_LIST);
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
  std::string json1 = R"({"jsonrpc": "2.0", "method": "test", "id": 1, "params": {"text": "line1\)";

  auto status = parser_->parse(json1);

  // Early termination.
  EXPECT_FALSE(status.ok());

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
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.name"),
      McpParserConfig::AttributeExtractionRule("params.custom_field"),
      McpParserConfig::AttributeExtractionRule("params.metadata.version")};

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
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.subscribe"),
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
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.a"),
      McpParserConfig::AttributeExtractionRule("params.a.b")};
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
  method_rule->add_extraction_rules()->set_path("params.field1");
  method_rule->add_extraction_rules()->set_path("params.field2");

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
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.level"),
      McpParserConfig::AttributeExtractionRule("params.other")};
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
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("int32_val"),
      McpParserConfig::AttributeExtractionRule("uint32_val"),
      McpParserConfig::AttributeExtractionRule("int64_val")};
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
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("bytes_val"),
      McpParserConfig::AttributeExtractionRule("float_val")};
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

TEST_F(McpJsonParserTest, ArrayWithNestedObjectsAndStrings) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "test_tool",
      "complexArray": [
        {"nested": "object", "value": 123},
        "string_in_array",
        {"another": "object"}
      ]
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  // Should extract params.name
  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "test_tool");

  // Array content should not be extracted
  const auto* complex_array = parser_->getNestedValue("params.complexArray");
  EXPECT_EQ(complex_array, nullptr);
}

TEST_F(McpJsonParserTest, JsonRpcBeforeMethod) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
      "name": "test_tool"
    }
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);
}

TEST_F(McpJsonParserTest, BoolInArrayAndAfterEarlyStop) {
  // Create a config that will cause early stop
  McpParserConfig custom_config;
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.name"),
  };
  custom_config.addMethodConfig("tools/call", rules);

  auto parser = std::make_unique<McpJsonParser>(custom_config);

  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "test",
      "boolArray": [true, false, true],
      "extraBool": false
    },
    "id": 1
  })";

  EXPECT_OK(parser->parse(json));

  EXPECT_TRUE(parser->isValidMcpRequest());

  // The bool values in array and after early stop should be skipped
  const auto* bool_array = parser->getNestedValue("params.boolArray");
  EXPECT_EQ(bool_array, nullptr);

  const auto* extra_bool = parser->getNestedValue("params.extraBool");
  EXPECT_EQ(extra_bool, nullptr);
}

TEST_F(McpJsonParserTest, ArraySkippingWithRequiredFieldAfter) {
  // This test ensures that we hit the array_depth_ > 0 checks in all Render methods.
  // We do this by requiring a field that comes AFTER the array, so early stop doesn't trigger.
  McpParserConfig custom_config;
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.end_field"),
  };
  custom_config.addMethodConfig("test", rules);

  auto parser = std::make_unique<McpJsonParser>(custom_config);

  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "test",
    "params": {
      "mixed_array": [
        "string_value",
        123,
        45.67,
        true,
        false,
        null,
        {"nested": "object"},
        [1, 2]
      ],
      "end_field": "required_value"
    },
    "id": 1
  })";

  EXPECT_OK(parser->parse(json));
  EXPECT_TRUE(parser->isValidMcpRequest());

  // Verify the required field was extracted
  const auto* end_field = parser->getNestedValue("params.end_field");
  ASSERT_NE(end_field, nullptr);
  EXPECT_EQ(end_field->string_value(), "required_value");

  // Verify nothing from the array was extracted (implied by parser logic, but good to check)
  const auto* array = parser->getNestedValue("params.mixed_array");
  EXPECT_EQ(array, nullptr);
}

TEST_F(McpJsonParserTest, MissingRequiredField) {
  // Valid MCP envelope, but missing required params.name for tools/call
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "other": "value"
    },
    "id": 1
  })";

  EXPECT_OK(parser_->parse(json));
  EXPECT_TRUE(parser_->isValidMcpRequest());

  // The required field should be missing
  const auto* name = parser_->getNestedValue("params.name");
  EXPECT_EQ(name, nullptr);

  // Other fields should still be extracted if they were in the config (but "other" is not)
  const auto* other = parser_->getNestedValue("params.other");
  EXPECT_EQ(other, nullptr);
}

TEST_F(McpJsonParserTest, MethodBeforeJsonRpc) {
  std::string json = R"({
    "method": "tools/call",
    "jsonrpc": "2.0",
    "params": {
      "name": "test_tool"
    },
    "id": 1
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), McpConstants::Methods::TOOLS_CALL);

  const auto* name = parser_->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "test_tool");
}

TEST_F(McpJsonParserTest, EmptyKeyIgnored) {
  // Ensure empty keys don't crash or cause issues
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "test",
      "": "empty_key_value"
    },
    "id": 1
  })";

  parseJson(json);
  EXPECT_TRUE(parser_->isValidMcpRequest());

  // Empty key should not be accessible/stored
  const auto* empty = parser_->getNestedValue("params.");
  EXPECT_EQ(empty, nullptr);
}

TEST_F(McpJsonParserTest, GetNestedValueEmptyPath) {
  // Test getNestedValue with empty path
  EXPECT_EQ(parser_->getNestedValue(""), nullptr);
}

TEST_F(McpJsonParserTest, CopyFieldByPathCoverage) {
  // Configure specific fields to test copyFieldByPath logic
  McpParserConfig custom_config;
  std::vector<McpParserConfig::AttributeExtractionRule> rules = {
      McpParserConfig::AttributeExtractionRule("params.name"),          // Parent field
      McpParserConfig::AttributeExtractionRule("params.missing.field"), // Field not found
      McpParserConfig::AttributeExtractionRule("params.name.child"),    // Intermediate not struct
      McpParserConfig::AttributeExtractionRule("deep.nested.value"),    // Deep nesting success
      McpParserConfig::AttributeExtractionRule("group.item1"),          // Group creation
      McpParserConfig::AttributeExtractionRule("group.item2")           // Group append
  };
  custom_config.addMethodConfig("test", rules);

  auto parser = std::make_unique<McpJsonParser>(custom_config);

  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "test",
    "params": {
      "name": "just_a_string"
    },
    "deep": {
      "nested": {
        "value": "found_it"
      }
    },
    "group": {
      "item1": "one",
      "item2": "two"
    },
    "id": 1
  })";

  EXPECT_OK(parser->parse(json));
  EXPECT_TRUE(parser->isValidMcpRequest());

  // Should just return without error, and not be in metadata
  EXPECT_EQ(parser->getNestedValue("params.missing.field"), nullptr);

  // "params.name" is "just_a_string", so .child cannot be traversed
  EXPECT_EQ(parser->getNestedValue("params.name.child"), nullptr);
  // Ensure the parent value is still there
  auto* name = parser->getNestedValue("params.name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value(), "just_a_string");

  auto* deep_val = parser->getNestedValue("deep.nested.value");
  ASSERT_NE(deep_val, nullptr);
  EXPECT_EQ(deep_val->string_value(), "found_it");

  auto* item1 = parser->getNestedValue("group.item1");
  ASSERT_NE(item1, nullptr);
  EXPECT_EQ(item1->string_value(), "one");

  auto* item2 = parser->getNestedValue("group.item2");
  ASSERT_NE(item2, nullptr);
  EXPECT_EQ(item2->string_value(), "two");
}

TEST_F(McpJsonParserTest, RootStringValue) {
  std::string json = R"("hello")";

  EXPECT_OK(parser_->parse(json));
  // Root string is not a valid MCP request but should be parsed without error
  // and trigger RenderString with empty name
  EXPECT_FALSE(parser_->isValidMcpRequest());
}

TEST_F(McpJsonParserTest, StringsInArray) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "test",
    "params": {
      "list": ["a", "b", "c"]
    },
    "id": 1
  })";

  EXPECT_OK(parser_->parse(json));
  EXPECT_TRUE(parser_->isValidMcpRequest());

  // Verify that strings inside the array were skipped (not extracted)
  // The parser logic for RenderString returns early if array_depth_ > 0
  const auto* list = parser_->getNestedValue("params.list");
  // Arrays are not stored in the metadata by McpFieldExtractor
  EXPECT_EQ(list, nullptr);
}

TEST_F(McpJsonParserTest, NotificationWithoutIdIsValid) {
  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "notifications/initialized"
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());
  EXPECT_EQ(parser_->getMethod(), "notifications/initialized");

  // id should not be present
  const auto* id = parser_->getNestedValue("id");
  EXPECT_EQ(id, nullptr);
}

TEST_F(McpJsonParserTest, CheckIdForRegularRequest) {
  std::string json = R"({
    "id": 2,
    "jsonrpc": "2.0",
    "params": {
      "name": "tool1"
    },
    "method": "tools/call"
  })";

  parseJson(json);

  EXPECT_TRUE(parser_->isValidMcpRequest());

  const auto* id = parser_->getNestedValue("id");
  ASSERT_NE(id, nullptr);
  EXPECT_EQ(id->number_value(), 2);
}

// Method Group Tests
TEST(McpParserConfigTest, BuiltInMethodGroups) {
  McpParserConfig config = McpParserConfig::createDefault();

  // Lifecycle
  EXPECT_EQ(config.getMethodGroup("initialize"), "lifecycle");
  EXPECT_EQ(config.getMethodGroup("notifications/initialized"), "lifecycle");
  EXPECT_EQ(config.getMethodGroup("ping"), "lifecycle");

  // Tool
  EXPECT_EQ(config.getMethodGroup("tools/call"), "tool");
  EXPECT_EQ(config.getMethodGroup("tools/list"), "tool");

  // Resource
  EXPECT_EQ(config.getMethodGroup("resources/read"), "resource");
  EXPECT_EQ(config.getMethodGroup("resources/list"), "resource");
  EXPECT_EQ(config.getMethodGroup("resources/subscribe"), "resource");
  EXPECT_EQ(config.getMethodGroup("resources/unsubscribe"), "resource");
  EXPECT_EQ(config.getMethodGroup("resources/templates/list"), "resource");

  // Prompt
  EXPECT_EQ(config.getMethodGroup("prompts/get"), "prompt");
  EXPECT_EQ(config.getMethodGroup("prompts/list"), "prompt");

  // Other built-ins
  EXPECT_EQ(config.getMethodGroup("logging/setLevel"), "logging");
  EXPECT_EQ(config.getMethodGroup("sampling/createMessage"), "sampling");
  EXPECT_EQ(config.getMethodGroup("completion/complete"), "completion");

  // Notifications (prefix match)
  EXPECT_EQ(config.getMethodGroup("notifications/progress"), "notification");
  EXPECT_EQ(config.getMethodGroup("notifications/cancelled"), "notification");
  EXPECT_EQ(config.getMethodGroup("notifications/custom"), "notification");

  // Unknown
  EXPECT_EQ(config.getMethodGroup("unknown/method"), "unknown");
  EXPECT_EQ(config.getMethodGroup("custom/extension"), "unknown");
}

TEST(McpParserConfigTest, MethodGroupFromProtoWithOverrides) {
  envoy::extensions::filters::http::mcp::v3::ParserConfig proto_config;
  proto_config.set_group_metadata_key("method_group");

  // Override initialize to be in "admin" group
  auto* method1 = proto_config.add_methods();
  method1->set_method("initialize");
  method1->set_group("admin");

  // Override tools/call to be in "operations" group
  auto* method2 = proto_config.add_methods();
  method2->set_method("tools/call");
  method2->set_group("operations");

  McpParserConfig config = McpParserConfig::fromProto(proto_config);

  EXPECT_EQ(config.groupMetadataKey(), "method_group");
  EXPECT_EQ(config.getMethodGroup("initialize"), "admin");
  EXPECT_EQ(config.getMethodGroup("tools/call"), "operations");

  // Non-overridden methods use built-in
  EXPECT_EQ(config.getMethodGroup("tools/list"), "tool");
  EXPECT_EQ(config.getMethodGroup("resources/read"), "resource");
  EXPECT_EQ(config.getMethodGroup("ping"), "lifecycle");
}

TEST(McpParserConfigTest, MethodGroupEmptyGroupFallsBackToBuiltIn) {
  envoy::extensions::filters::http::mcp::v3::ParserConfig proto_config;
  proto_config.set_group_metadata_key("group");

  // Empty group means use built-in
  auto* method = proto_config.add_methods();
  method->set_method("tools/call");
  method->set_group(""); // Empty group

  McpParserConfig config = McpParserConfig::fromProto(proto_config);

  // Should fall back to built-in group
  EXPECT_EQ(config.getMethodGroup("tools/call"), "tool");
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
