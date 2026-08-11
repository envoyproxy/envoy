#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/schema_registry.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace OpenAi {
namespace {

using StatusHelpers::IsOk;
using StatusHelpers::StatusCodeIs;

TEST(OpenAiChatCompletionsTest, StandardValidPayload) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json valid_req = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "system"}, {"content", "You are a helpful assistant."}},
                       {{"role", "user"}, {"content", "Hello!"}},
                   })},
      {"temperature", 0.7},
      {"max_tokens", 100},
      {"stream", false},
  };
  EXPECT_THAT(payload_schema.validateRequest(valid_req), IsOk());
}

TEST(OpenAiChatCompletionsTest, OffloadedMessageContent) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json offloaded_req = {
      {"model", "gpt-4o"},
      {"messages",
       nlohmann::json::array({
           {{"role", "user"},
            {"content", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{100, 50000})}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_req), IsOk());
}

TEST(OpenAiChatCompletionsTest, MultimodalContentParts) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json multimodal_req = {
      {"model", "gpt-4o"},
      {"messages",
       nlohmann::json::array(
           {{{"role", "user"},
             {"content", nlohmann::json::array(
                             {{{"type", "text"}, {"text", "What is in this image?"}},
                              {{"type", "image_url"},
                               {"image_url", {{"url", "https://example.com/image.png"}}}}})}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(multimodal_req), IsOk());
}

TEST(OpenAiChatCompletionsTest, ToolsAndToolCalls) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json tools_req = {
      {"model", "gpt-4o"},
      {"messages",
       nlohmann::json::array({
           {{"role", "assistant"},
            {"tool_calls", nlohmann::json::array({
                               {{"id", "call_123"},
                                {"type", "function"},
                                {"function",
                                 {{"name", "get_weather"},
                                  {"arguments", JsonWithExtBuf::makeExternalRef(
                                                    JsonWithExtBuf::ExternalRef{500, 200})}}}},
                           })}},
       })},
      {"tools",
       nlohmann::json::array({
           {{"type", "function"},
            {"function",
             {{"name", "get_weather"},
              {"description", "Get current weather"},
              {"parameters", {{"type", "object"}, {"properties", {{"location", "string"}}}}}}}},
       })},
      {"tool_choice", "auto"},
  };
  EXPECT_THAT(payload_schema.validateRequest(tools_req), IsOk());
}

TEST(OpenAiChatCompletionsTest, UnknownFieldsPassThrough) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json custom_fields_req = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
      {"custom_routing_tag", "blue"},
      {"user_tracking_id", 9999},
  };
  EXPECT_THAT(payload_schema.validateRequest(custom_fields_req), IsOk());
}

TEST(OpenAiChatCompletionsTest, MissingRequiredFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Missing required model.
  nlohmann::json missing_model = {
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_model),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Missing required messages.
  nlohmann::json missing_messages = {
      {"model", "gpt-4o"},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_messages),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Empty messages array (min size is 1).
  nlohmann::json empty_messages = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array()},
  };
  EXPECT_THAT(payload_schema.validateRequest(empty_messages),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Missing required role in message.
  nlohmann::json missing_role = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"content", "Hi"}},
                   })},
  };
  auto role_err = payload_schema.validateRequest(missing_role);
  EXPECT_THAT(role_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(role_err.message(), "missing required field: /messages/0/role");
}

TEST(OpenAiChatCompletionsTest, InvalidFieldValuesAndTypes) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Invalid role enum value.
  nlohmann::json invalid_role = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "admin"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(invalid_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Invalid model offload (model cannot be an ExternalRef).
  nlohmann::json offloaded_model = {
      {"model", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 100})},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_model),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Temperature out of bounds.
  nlohmann::json high_temp = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
      {"temperature", 2.5},
  };
  EXPECT_THAT(payload_schema.validateRequest(high_temp),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(OpenAiChatCompletionsTest, SchemaRegistryLookup) {
  const PayloadSchema* schema = SchemaRegistry::getSchema(PerRouteProto::OPENAI_CHAT_COMPLETIONS);
  ASSERT_NE(schema, nullptr);

  const PayloadSchema* unspec_schema = SchemaRegistry::getSchema(PerRouteProto::UNSPECIFIED);
  EXPECT_EQ(unspec_schema, nullptr);
}

} // namespace
} // namespace OpenAi
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
