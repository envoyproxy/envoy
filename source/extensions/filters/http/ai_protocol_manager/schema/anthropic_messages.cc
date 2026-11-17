#include "source/extensions/filters/http/ai_protocol_manager/schema/anthropic_messages.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Anthropic {

// The parser stores any string longer than the inline threshold (1kb by default) as an
// external buffer reference instead of an inline string. Validation will reject such a
// reference unless the field is marked `.offloadable()`.

// A text block, the only block kind `system` accepts.
const Schema& textBlockSchema() {
  static const Schema schema = Schema::object({
      {"type", Schema::enumString({"text"}).required()},
      {"text", Schema::string().offloadable().required()},
  });
  return schema;
}

// Lists every field any content block kind can have, in one flat object. Which ones are
// actually valid depends on `type`, but we do not check that: each field is type checked
// when present, and the upstream API rejects bad combinations.
const Schema& contentBlockSchema() {
  static const Schema schema = Schema::object({
      {"type", Schema::string().required()},
      {"text", Schema::string().offloadable()},
      {"thinking", Schema::string().offloadable()},
      {"signature", Schema::string().offloadable()},
      {"data", Schema::string().offloadable()},
      // `search_result` blocks send `source` as a bare string, not as the object `image` and
      // `document` blocks send:
      // https://github.com/anthropics/anthropic-sdk-python/blob/main/src/anthropic/types/search_result_block_param.py
      {"source", Schema::oneOf({
                     Schema::object({
                         {"type", Schema::string().required()},
                         {"media_type", Schema::string()},
                         {"data", Schema::string().offloadable()},
                         {"url", Schema::string().offloadable()},
                         {"file_id", Schema::string()},
                     }),
                     Schema::string().offloadable(),
                 })},
      {"id", Schema::string()},
      {"name", Schema::string()},
      // `input` on a `tool_use` block holds the arguments Claude passes to the tool. Its
      // members are defined by that tool's own `input_schema`, so all this can assert is that
      // it is an object.
      {"input", Schema::object({}).allowUnknownFields(true)},
      {"tool_use_id", Schema::string()},
      // `content` holds information returned by a tool, and its shape varies by block kind: a
      // plain string, an array of blocks, or an object. Only the string form is a leaf
      // value, so only it can be offloaded.
      {"content", Schema::oneOf({
                      Schema::string().offloadable(),
                      Schema::array(Schema::any()),
                      Schema::object({}).allowUnknownFields(true),
                  })},
      {"is_error", Schema::boolean()},
      // `title` and `context` are optional on a `document` block, so a client may send them as
      // an explicit null:
      // https://github.com/anthropics/anthropic-sdk-python/blob/main/src/anthropic/types/document_block_param.py
      {"title", Schema::string().offloadable().nullable()},
      {"context", Schema::string().offloadable().nullable()},
  });
  return schema;
}

const Schema& messageSchema() {
  static const Schema schema = Schema::object({
      {"role", Schema::enumString({"user", "assistant"}).required()},
      {"content", Schema::oneOf({
                                    Schema::string().offloadable(),
                                    Schema::array(contentBlockSchema()),
                                })
                      .required()},
  });
  return schema;
}

const Schema& toolSchema() {
  static const Schema schema = Schema::object({
      {"name", Schema::string().required()},
      {"type", Schema::string().nullable()},
      {"description", Schema::string().offloadable().nullable()},
      {"input_schema", Schema::object({}).allowUnknownFields(true)},
  });
  return schema;
}

PayloadSchema createPayloadSchema() {
  return PayloadSchema{
      /*request_schema=*/RequestSchema{
          Schema::object({
              {"model", Schema::string().required()},
              // Zero is valid: it is how a caller pre-warms the prompt cache, asking for the
              // input to be processed and cached without any output being generated.
              {"max_tokens", Schema::integer().min(0).required()},
              {"messages", Schema::array(messageSchema()).min(1).required()},
              {"system", Schema::oneOf({
                             Schema::string().offloadable(),
                             Schema::array(textBlockSchema()),
                         }).nullable()},
              {"temperature", Schema::number().range(0.0, 1.0).nullable()},
              {"top_p", Schema::number().range(0.0, 1.0).nullable()},
              {"top_k", Schema::integer().min(0).nullable()},
              {"stream", Schema::boolean().nullable()},
              {"stop_sequences", Schema::array(Schema::string()).nullable()},
              {"metadata", Schema::object({
                               {"user_id", Schema::string().nullable()},
                           }).nullable()},
              {"service_tier", Schema::string().nullable()},
              // `budget_tokens` applies to the `enabled` variant only; `adaptive` and
              // `disabled` omit it, so it cannot be required here.
              {"thinking", Schema::object({
                               {"type", Schema::enumString({"enabled", "disabled", "adaptive"})
                                            .required()},
                               {"budget_tokens", Schema::integer().min(0).nullable()},
                           }).nullable()},
              {"tools", Schema::array(toolSchema()).nullable()},
              {"tool_choice",
               Schema::object({
                   {"type", Schema::enumString({"auto", "any", "tool", "none"}).required()},
                   {"name", Schema::string().nullable()},
                   {"disable_parallel_tool_use", Schema::boolean().nullable()},
               }).nullable()},
          }),
          /*streamable_field_order=*/{
              "messages[].content",
              "messages[].content[].text",
              "messages[].content[].thinking",
              "messages[].content[].signature",
              "messages[].content[].data",
              "messages[].content[].source.data",
              "messages[].content[].source.url",
              "messages[].content[].source",
              "messages[].content[].content",
              "messages[].content[].title",
              "messages[].content[].context",
              "system",
              "system[].text",
              "tools[].description",
          }},
      /*response_schema=*/ResponseSchema{}};
}

} // namespace Anthropic
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
