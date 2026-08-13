#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace OpenAI {
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
             {"content",
              nlohmann::json::array(
                  {{{"type", "text"}, {"text", "What is in this image?"}},
                   {{"type", "image_url"},
                    {"image_url", {{"url", "https://example.com/image.png"}}}},
                   {{"type", "image_url"},
                    {"image_url",
                     {{"url", "data:image/jpeg;base64,/9j/4AAQSkZJRg..."}, {"detail", "high"}}}},
                   {{"type", "image_url"},
                    {"image_url",
                     {{"url",
                       JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{100, 50000})},
                      {"detail", "auto"}}}}})}}})},
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
  EXPECT_EQ(role_err.message(), "missing required field: messages[0].role");

  // Missing required url in image_url part.
  nlohmann::json missing_image_url = {
      {"model", "gpt-4o"},
      {"messages",
       nlohmann::json::array({
           {{"role", "user"},
            {"content", nlohmann::json::array({
                            {{"type", "image_url"}, {"image_url", {{"detail", "high"}}}},
                        })}},
       })},
  };
  auto img_err = payload_schema.validateRequest(missing_image_url);
  EXPECT_THAT(img_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(img_err.message(),
              testing::HasSubstr("missing required field: messages[0].content[0].image_url.url"));
}

TEST(OpenAiChatCompletionsTest, CanonicalStreamableFieldOrder) {
  PayloadSchema payload_schema = createPayloadSchema();
  const std::vector<std::string> expected_order = {
      "messages[].content",
      "messages[].content[].text",
      "messages[].content[].image_url.url",
      "messages[].tool_calls[].function.arguments",
      "tools[].function.description",
  };
  EXPECT_EQ(payload_schema.requestStreamableFieldOrder(), expected_order);
  EXPECT_EQ(payload_schema.requestOffloadableFieldPaths(), expected_order);
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

  // Invalid image_url.url type (e.g., integer instead of string or ExternalRef).
  nlohmann::json invalid_image_url_type = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "user"},
                        {"content", nlohmann::json::array({
                                        {{"type", "image_url"}, {"image_url", {{"url", 12345}}}},
                                    })}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(invalid_image_url_type),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Non-offloadable field in image_url (e.g. detail) cannot be an ExternalRef.
  nlohmann::json offloaded_detail = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "user"},
                        {"content", nlohmann::json::array({
                                        {{"type", "image_url"},
                                         {"image_url",
                                          {{"url", "https://example.com/image.png"},
                                           {"detail", JsonWithExtBuf::makeExternalRef(
                                                          JsonWithExtBuf::ExternalRef{0, 10})}}}},
                                    })}},
                   })},
  };
  auto detail_err = payload_schema.validateRequest(offloaded_detail);
  EXPECT_THAT(detail_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(detail_err.message(),
              testing::HasSubstr("field 'messages[0].content[0].image_url.detail' cannot be "
                                 "offloaded to external buffer"));
}

TEST(OpenAiChatCompletionsTest, SubSchemasDirectValidation) {
  // Test toolCallSchema directly.
  const Schema& tool_call_schema = toolCallSchema();
  nlohmann::json valid_tool_call = {
      {"id", "call_1"},
      {"type", "function"},
      {"function",
       {{"name", "search"},
        {"arguments", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 100})}}},
  };
  EXPECT_THAT(tool_call_schema.validate(valid_tool_call), IsOk());

  nlohmann::json invalid_tool_call = {
      {"id", "call_1"},
      {"type", "unknown_type"},
  };
  EXPECT_THAT(tool_call_schema.validate(invalid_tool_call),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Test chatMessageSchema directly.
  const Schema& chat_message_schema = chatMessageSchema();
  nlohmann::json valid_msg = {
      {"role", "assistant"}, {"content", "Hello"},       {"refusal", "Cannot comply"},
      {"name", "bot"},       {"tool_call_id", "call_1"},
  };
  EXPECT_THAT(chat_message_schema.validate(valid_msg), IsOk());

  // Direct validation of chatMessageSchema with offloaded image_url.
  nlohmann::json multimodal_msg = {
      {"role", "user"},
      {"content",
       nlohmann::json::array({
           {{"type", "image_url"},
            {"image_url",
             {{"url", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{100, 5000})}}}},
       })},
  };
  EXPECT_THAT(chat_message_schema.validate(multimodal_msg), IsOk());

  nlohmann::json msg_invalid_role = {
      {"role", "superadmin"},
  };
  EXPECT_THAT(chat_message_schema.validate(msg_invalid_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Test toolSchema directly.
  const Schema& tool_schema = toolSchema();
  nlohmann::json valid_tool = {
      {"type", "function"},
      {"function",
       {{"name", "fetch_info"},
        {"description", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 50})}}},
  };
  EXPECT_THAT(tool_schema.validate(valid_tool), IsOk());

  nlohmann::json invalid_tool = {
      {"type", "not_a_function"},
  };
  EXPECT_THAT(tool_schema.validate(invalid_tool), StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Test toolSchema with nullable optional fields.
  nlohmann::json null_fields_tool = {
      {"type", "function"},
      {"function", {{"name", "fetch_info"}, {"description", nullptr}, {"strict", nullptr}}},
  };
  EXPECT_THAT(tool_schema.validate(null_fields_tool), IsOk());
}

TEST(OpenAiChatCompletionsTest, NullableFieldsValidation) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Full request payload with all optional fields explicitly set to null.
  nlohmann::json null_fields_req = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", "user"},
                        {"content", nullptr},
                        {"name", nullptr},
                        {"tool_call_id", nullptr},
                        {"tool_calls", nullptr}},
                   })},
      {"temperature", nullptr},
      {"top_p", nullptr},
      {"n", nullptr},
      {"stream", nullptr},
      {"stop", nullptr},
      {"max_tokens", nullptr},
      {"max_completion_tokens", nullptr},
      {"presence_penalty", nullptr},
      {"frequency_penalty", nullptr},
      {"logit_bias", nullptr},
      {"user", nullptr},
      {"tools", nullptr},
      {"tool_choice", nullptr},
      {"response_format", nullptr},
      {"seed", nullptr},
      {"service_tier", nullptr},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_fields_req), IsOk());

  // Non-nullable fields set to null must be rejected.
  nlohmann::json null_model = {
      {"model", nullptr},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_model),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_messages = {
      {"model", "gpt-4o"},
      {"messages", nullptr},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_messages),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_role = {
      {"model", "gpt-4o"},
      {"messages", nlohmann::json::array({
                       {{"role", nullptr}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace OpenAI
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
