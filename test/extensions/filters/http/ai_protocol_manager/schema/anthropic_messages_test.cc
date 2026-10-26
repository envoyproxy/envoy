#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/anthropic_messages.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Anthropic {
namespace {

using StatusHelpers::IsOk;
using StatusHelpers::StatusCodeIs;

TEST(AnthropicMessagesTest, StandardValidPayload) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json valid_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hello!"}},
                       {{"role", "assistant"}, {"content", "Hi, how can I help?"}},
                   })},
      {"system", "You are a helpful assistant."},
      {"temperature", 0.7},
      {"stream", false},
  };
  EXPECT_THAT(payload_schema.validateRequest(valid_req), IsOk());
}

// Zero is how a caller pre-warms the prompt cache, so it must validate.
TEST(AnthropicMessagesTest, ZeroMaxTokensPrewarmsTheCache) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json prewarm_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 0},
      {"messages", nlohmann::json::array({
                       {{"role", "user"},
                        {"content", nlohmann::json::array({
                                        {{"type", "text"},
                                         {"text", "Long shared prefix."},
                                         {"cache_control", {{"type", "ephemeral"}}}},
                                    })}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(prewarm_req), IsOk());
}

TEST(AnthropicMessagesTest, OffloadedMessageContent) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json offloaded_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array({
           {{"role", "user"},
            {"content", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{100, 50000})}},
       })},
      {"system", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{10, 4096})},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_req), IsOk());
}

TEST(AnthropicMessagesTest, ContentBlocks) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json blocks_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array(
           {{{"role", "user"},
             {"content",
              nlohmann::json::array(
                  {{{"type", "text"}, {"text", "What is in this image?"}},
                   {{"type", "text"},
                    {"text",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{200, 30000})}},
                   {{"type", "image"},
                    {"source",
                     {{"type", "base64"},
                      {"media_type", "image/jpeg"},
                      {"data",
                       JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 100000})}}}},
                   {{"type", "image"},
                    {"source", {{"type", "url"}, {"url", "https://example.com/image.png"}}}},
                   {{"type", "document"},
                    {"title", "Contract"},
                    {"context",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{5, 9000})}}})}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(blocks_req), IsOk());
}

// `search_result` blocks carry `source` as a bare string, unlike the object `image` and
// `document` blocks carry; both forms must validate.
TEST(AnthropicMessagesTest, SearchResultBlockStringSource) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json search_result_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array(
           {{{"role", "user"},
             {"content",
              nlohmann::json::array(
                  {{{"type", "search_result"},
                    {"source", "https://example.com/article"},
                    {"title", "Example Article"},
                    {"content", nlohmann::json::array({{{"type", "text"}, {"text", "Passage."}}})},
                    {"citations", {{"enabled", true}}}},
                   {{"type", "search_result"},
                    {"source",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{300, 2048})},
                    {"title", "Offloaded Source"},
                    {"content",
                     nlohmann::json::array({{{"type", "text"}, {"text", "Passage."}}})}}})}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(search_result_req), IsOk());
}

// `source.url` and `title` are declared offloadable but every other test sends them
// inline, where the marking is not load bearing. Exercise them offloaded so a dropped
// `.offloadable()` on either one fails here.
TEST(AnthropicMessagesTest, OffloadedSourceUrlAndTitle) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json offloaded_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array(
           {{{"role", "user"},
             {"content",
              nlohmann::json::array(
                  {{{"type", "image"},
                    // A data URL long enough to be offloaded by the parser.
                    {"source",
                     {{"type", "url"},
                      {"url",
                       JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 40000})}}}},
                   {{"type", "document"},
                    {"source", {{"type", "url"}, {"url", "https://example.com/spec.pdf"}}},
                    {"title", JsonWithExtBuf::makeExternalRef(
                                  JsonWithExtBuf::ExternalRef{40000, 2048})}}})}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_req), IsOk());
}

TEST(AnthropicMessagesTest, ThinkingAndToolBlocks) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json thinking_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 2048},
      {"messages",
       nlohmann::json::array(
           {{{"role", "assistant"},
             {"content",
              nlohmann::json::array({{{"type", "thinking"},
                                      {"thinking", JsonWithExtBuf::makeExternalRef(
                                                       JsonWithExtBuf::ExternalRef{0, 20000})},
                                      {"signature", JsonWithExtBuf::makeExternalRef(
                                                        JsonWithExtBuf::ExternalRef{20000, 800})}},
                                     {{"type", "redacted_thinking"},
                                      {"data", JsonWithExtBuf::makeExternalRef(
                                                   JsonWithExtBuf::ExternalRef{21000, 500})}},
                                     {{"type", "tool_use"},
                                      {"id", "toolu_123"},
                                      {"name", "get_weather"},
                                      {"input", {{"location", "San Francisco"}}}}})}},
            {{"role", "user"},
             {"content", nlohmann::json::array({{{"type", "tool_result"},
                                                 {"tool_use_id", "toolu_123"},
                                                 {"content", "62 degrees and foggy"},
                                                 {"is_error", false}}})}}})},
      {"thinking", {{"type", "enabled"}, {"budget_tokens", 1024}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(thinking_req), IsOk());
}

// The members the schema declares on tool and document blocks: a tool-defined `input` object,
// `content` in both its string and block-array forms, `is_error`, `title`, and the `source`
// members `media_type` and `file_id`.
TEST(AnthropicMessagesTest, ToolAndDocumentBlockMembers) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json members_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array(
           {{{"role", "assistant"},
             {"content", nlohmann::json::array({{{"type", "tool_use"},
                                                 {"id", "toolu_1"},
                                                 {"name", "search"},
                                                 {"input", {{"query", "envoy"}, {"limit", 5}}}}})}},
            {{"role", "user"},
             {"content",
              nlohmann::json::array(
                  {// A tool result over the inline threshold arrives offloaded.
                   {{"type", "tool_result"},
                    {"tool_use_id", "toolu_1"},
                    {"is_error", false},
                    {"content",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 50000})}},
                   {{"type", "tool_result"},
                    {"tool_use_id", "toolu_2"},
                    {"content", nlohmann::json::array({{{"type", "text"}, {"text", "62F"}}})}},
                   {{"type", "document"},
                    {"title", "Contract"},
                    {"source", {{"type", "file"}, {"file_id", "file_123"}}}},
                   {{"type", "image"},
                    {"source",
                     {{"type", "base64"},
                      {"media_type", "image/png"},
                      {"data", JsonWithExtBuf::makeExternalRef(
                                   JsonWithExtBuf::ExternalRef{1000, 40000})}}}}})}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(members_req), IsOk());
}

TEST(AnthropicMessagesTest, SystemBlocksAndTools) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json tools_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "What is the weather?"}},
                   })},
      {"system",
       nlohmann::json::array(
           {{{"type", "text"}, {"text", "You are a weather bot."}},
            {{"type", "text"},
             {"text", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 8000})},
             {"cache_control", {{"type", "ephemeral"}}}}})},
      {"tools",
       nlohmann::json::array({
           {{"name", "get_weather"},
            {"description",
             JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{9000, 2000})},
            {"input_schema", {{"type", "object"}, {"properties", {{"location", "string"}}}}}},
           {{"type", "web_search_20250305"}, {"name", "web_search"}},
       })},
      {"tool_choice", {{"type", "tool"}, {"name", "get_weather"}}},
      {"stop_sequences", nlohmann::json::array({"END"})},
      {"metadata", {{"user_id", "user_123"}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(tools_req), IsOk());
}

TEST(AnthropicMessagesTest, UnknownFieldsPassThrough) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Unrecognized top-level parameters, block kinds, and block members must not be
  // rejected: the wire API gains them without a version bump.
  nlohmann::json custom_fields_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"},
                        {"content", nlohmann::json::array({
                                        {{"type", "some_future_block"}, {"payload", 42}},
                                    })}},
                   })},
      {"mcp_servers", nlohmann::json::array()},
      {"custom_routing_tag", "blue"},
  };
  EXPECT_THAT(payload_schema.validateRequest(custom_fields_req), IsOk());
}

TEST(AnthropicMessagesTest, OffloadedValueInUndeclaredFieldIsAccepted) {
  PayloadSchema payload_schema = createPayloadSchema();

  // The parser offloads any string over the inline threshold, whether or not the schema
  // declares the field holding it. Such a value is accepted today only because validation
  // never descends into positions the schema does not declare: unknown block members, the
  // members of `tool_use.input`, and the elements of `tool_result.content`. If that ever
  // changes, every large string in those positions starts returning 400 on valid traffic,
  // and this test is what should fail first.
  nlohmann::json offloaded_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array(
           {{{"role", "assistant"},
             {"content", nlohmann::json::array(
                             {{{"type", "tool_use"},
                               {"id", "toolu_1"},
                               {"name", "edit_file"},
                               // A member of `input`, shaped by the tool's own `input_schema`.
                               {"input",
                                {{"patch", JsonWithExtBuf::makeExternalRef(
                                               JsonWithExtBuf::ExternalRef{0, 60000})}}}},
                              // A member the schema does not declare at all.
                              {{"type", "some_future_block"},
                               {"payload", JsonWithExtBuf::makeExternalRef(
                                               JsonWithExtBuf::ExternalRef{60000, 5000})}}})}},
            {{"role", "user"},
             {"content", nlohmann::json::array(
                             {{{"type", "tool_result"},
                               {"tool_use_id", "toolu_1"},
                               // An element of the array form of `content`.
                               {"content", nlohmann::json::array(
                                               {{{"type", "text"},
                                                 {"text", JsonWithExtBuf::makeExternalRef(
                                                              JsonWithExtBuf::ExternalRef{
                                                                  65000, 70000})}}})}}})}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_req), IsOk());
}

TEST(AnthropicMessagesTest, MissingRequiredFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Missing required model.
  nlohmann::json missing_model = {
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  auto model_err = payload_schema.validateRequest(missing_model);
  EXPECT_THAT(model_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(model_err.message(), "missing required field: model");

  // Missing required max_tokens.
  nlohmann::json missing_max_tokens = {
      {"model", "claude-sonnet-4-20250514"},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  auto max_tokens_err = payload_schema.validateRequest(missing_max_tokens);
  EXPECT_THAT(max_tokens_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(max_tokens_err.message(), "missing required field: max_tokens");

  // Missing required messages.
  nlohmann::json missing_messages = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_messages),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Empty messages array (min size is 1).
  nlohmann::json empty_messages = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array()},
  };
  EXPECT_THAT(payload_schema.validateRequest(empty_messages),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Missing required role in message.
  nlohmann::json missing_role = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"content", "Hi"}},
                   })},
  };
  auto role_err = payload_schema.validateRequest(missing_role);
  EXPECT_THAT(role_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(role_err.message(), "missing required field: messages[0].role");

  // Missing required content in message.
  nlohmann::json missing_content = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}},
                   })},
  };
  auto content_err = payload_schema.validateRequest(missing_content);
  EXPECT_THAT(content_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(content_err.message(), "missing required field: messages[0].content");

  // Missing required text in a system text block.
  nlohmann::json missing_system_text = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
      {"system", nlohmann::json::array({{{"type", "text"}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_system_text),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(AnthropicMessagesTest, InvalidFieldValuesAndTypes) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Non-object request body.
  EXPECT_THAT(payload_schema.validateRequest(nlohmann::json::array()),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Invalid role enum value: Anthropic has no system role in the messages array.
  nlohmann::json invalid_role = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "system"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(invalid_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Non-string model.
  nlohmann::json numeric_model = {
      {"model", 123},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(numeric_model),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // max_tokens must be a non-negative integer.
  nlohmann::json negative_max_tokens = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", -1},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(negative_max_tokens),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json fractional_max_tokens = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 10.5},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(fractional_max_tokens),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Temperature is bounded at 1.0, unlike OpenAI's 2.0.
  nlohmann::json high_temp = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
      {"temperature", 1.5},
  };
  EXPECT_THAT(payload_schema.validateRequest(high_temp),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Invalid tool_choice type.
  nlohmann::json invalid_tool_choice = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
      {"tool_choice", {{"type", "required"}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(invalid_tool_choice),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Content must be a string or an array, not a bare object.
  nlohmann::json object_content = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", {{"text", "Hi"}}}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(object_content),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(AnthropicMessagesTest, NumericBoundsAndNestedEnums) {
  PayloadSchema payload_schema = createPayloadSchema();

  // A minimal valid request with one extra top-level parameter set.
  const auto with_param = [](const std::string& key, const nlohmann::json& value) {
    nlohmann::json req = {
        {"model", "claude-sonnet-4-20250514"},
        {"max_tokens", 1024},
        {"messages", nlohmann::json::array({
                         {{"role", "user"}, {"content", "Hi"}},
                     })},
    };
    req[key] = value;
    return req;
  };

  // Sampling bounds. Anthropic caps temperature and top_p at 1.0.
  EXPECT_THAT(payload_schema.validateRequest(with_param("temperature", -0.1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_param("top_p", 1.5)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_param("top_p", -0.1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_param("top_k", -1)),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // The bounds are inclusive.
  EXPECT_THAT(payload_schema.validateRequest(with_param("temperature", 1.0)), IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_param("top_p", 0.0)), IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_param("top_k", 0)), IsOk());

  // `thinking.type` is a closed enum, and is required once `thinking` is present.
  EXPECT_THAT(payload_schema.validateRequest(
                  with_param("thinking", {{"type", "on"}, {"budget_tokens", 1024}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  auto thinking_err =
      payload_schema.validateRequest(with_param("thinking", {{"budget_tokens", 1024}}));
  EXPECT_THAT(thinking_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(thinking_err.message(), "missing required field: thinking.type");
  EXPECT_THAT(payload_schema.validateRequest(
                  with_param("thinking", {{"type", "enabled"}, {"budget_tokens", -1}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_param("thinking", {{"type", "disabled"}})),
              IsOk());

  // Element and member types inside the optional containers.
  EXPECT_THAT(
      payload_schema.validateRequest(with_param("stop_sequences", nlohmann::json::array({1, 2}))),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_param("metadata", {{"user_id", 42}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      payload_schema.validateRequest(with_param(
          "tool_choice",
          {{"type", "tool"}, {"name", "get_weather"}, {"disable_parallel_tool_use", true}})),
      IsOk());

  // `source.type` is required whenever the object form of `source` is present. The oneOf
  // aggregates every candidate's failure, so the object candidate's message is a substring.
  nlohmann::json missing_source_type = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array({
           {{"role", "user"},
            {"content", nlohmann::json::array({
                            {{"type", "image"}, {"source", {{"media_type", "image/png"}}}},
                        })}},
       })},
  };
  auto source_err = payload_schema.validateRequest(missing_source_type);
  EXPECT_THAT(source_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(source_err.message(),
              testing::HasSubstr("missing required field: messages[0].content[0].source.type"));

  // `role` is required and not nullable, so an explicit null is not the same as omission.
  nlohmann::json null_role = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", nullptr}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(AnthropicMessagesTest, NonOffloadableFieldsRejectExternalRefs) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Routing metadata must stay inline and readable.
  nlohmann::json offloaded_model = {
      {"model", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 100})},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  auto model_err = payload_schema.validateRequest(offloaded_model);
  EXPECT_THAT(model_err, StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(model_err.message(), "field 'model' cannot be offloaded to external buffer");

  nlohmann::json offloaded_role = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array({
           {{"role", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 10})},
            {"content", "Hi"}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json offloaded_block_type = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array({
           {{"role", "user"},
            {"content",
             nlohmann::json::array({
                 {{"type", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 10})},
                  {"text", "Hi"}},
             })}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_block_type),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(AnthropicMessagesTest, CanonicalStreamableFieldOrder) {
  PayloadSchema payload_schema = createPayloadSchema();
  const std::vector<std::string> expected_order = {
      "messages[].content",
      "messages[].content[].text",
      "messages[].content[].thinking",
      "messages[].content[].signature",
      "messages[].content[].data",
      "messages[].content[].source.data",
      "messages[].content[].source.url",
      // The `search_result` string form of `source`, from the oneOf's second candidate.
      "messages[].content[].source",
      "messages[].content[].content",
      "messages[].content[].title",
      "messages[].content[].context",
      "system",
      "system[].text",
      "tools[].description",
  };
  EXPECT_EQ(payload_schema.requestStreamableFieldOrder(), expected_order);
  EXPECT_EQ(payload_schema.requestOffloadableFieldPaths(), expected_order);
}

TEST(AnthropicMessagesTest, NullableFieldsValidation) {
  PayloadSchema payload_schema = createPayloadSchema();

  // Every optional parameter explicitly set to null.
  nlohmann::json null_fields_req = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
      {"system", nullptr},
      {"temperature", nullptr},
      {"top_p", nullptr},
      {"top_k", nullptr},
      {"stream", nullptr},
      {"stop_sequences", nullptr},
      {"metadata", nullptr},
      {"service_tier", nullptr},
      {"thinking", nullptr},
      {"tools", nullptr},
      {"tool_choice", nullptr},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_fields_req), IsOk());

  // Required fields set to null must be rejected.
  nlohmann::json null_model = {
      {"model", nullptr},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", "Hi"}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_model),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_messages = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nullptr},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_messages),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_content = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"}, {"content", nullptr}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_content),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(AnthropicMessagesTest, NullableContentBlockFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  // `title` and `context` are optional on a `document` block, so an explicit null is accepted.
  nlohmann::json null_document_members = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages", nlohmann::json::array({
                       {{"role", "user"},
                        {"content", nlohmann::json::array({
                                        {{"type", "document"},
                                         {"source", {{"type", "text"}, {"data", "Report body"}}},
                                         {"title", nullptr},
                                         {"context", nullptr}},
                                    })}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_document_members), IsOk());

  // `is_error` on a `tool_result` block may be left out but is never null, so an explicit null
  // stays a rejection.
  nlohmann::json null_tool_result_members = {
      {"model", "claude-sonnet-4-20250514"},
      {"max_tokens", 1024},
      {"messages",
       nlohmann::json::array({
           {{"role", "user"},
            {"content",
             nlohmann::json::array({
                 {{"type", "tool_result"}, {"tool_use_id", "toolu_01A"}, {"is_error", nullptr}},
             })}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_tool_result_members),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(AnthropicMessagesTest, SubSchemasDirectValidation) {
  // messageSchema.
  const Schema& message_schema = messageSchema();
  nlohmann::json valid_msg = {{"role", "user"}, {"content", "Hello"}};
  EXPECT_THAT(message_schema.validate(valid_msg), IsOk());

  nlohmann::json invalid_msg_role = {{"role", "tool"}, {"content", "Hello"}};
  EXPECT_THAT(message_schema.validate(invalid_msg_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // contentBlockSchema.
  const Schema& content_block_schema = contentBlockSchema();
  nlohmann::json valid_block = {
      {"type", "text"},
      {"text", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 100})},
  };
  EXPECT_THAT(content_block_schema.validate(valid_block), IsOk());

  nlohmann::json block_missing_type = {{"text", "Hi"}};
  EXPECT_THAT(content_block_schema.validate(block_missing_type),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // textBlockSchema accepts only text blocks.
  const Schema& text_block_schema = textBlockSchema();
  nlohmann::json valid_text_block = {{"type", "text"}, {"text", "Hi"}};
  EXPECT_THAT(text_block_schema.validate(valid_text_block), IsOk());

  nlohmann::json image_block = {{"type", "image"}, {"text", "Hi"}};
  EXPECT_THAT(text_block_schema.validate(image_block),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // toolSchema.
  const Schema& tool_schema = toolSchema();
  nlohmann::json valid_tool = {
      {"name", "get_weather"},
      {"description", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 50})},
      {"input_schema", {{"type", "object"}}},
  };
  EXPECT_THAT(tool_schema.validate(valid_tool), IsOk());

  nlohmann::json tool_missing_name = {{"input_schema", {{"type", "object"}}}};
  EXPECT_THAT(tool_schema.validate(tool_missing_name),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_fields_tool = {{"name", "get_weather"},
                                     {"type", nullptr},
                                     {"description", nullptr},
                                     {"cache_control", nullptr}};
  EXPECT_THAT(tool_schema.validate(null_fields_tool), IsOk());
}

} // namespace
} // namespace Anthropic
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
