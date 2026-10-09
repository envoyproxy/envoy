#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace OpenAI {

const Schema& toolCallSchema() {
  static const Schema schema = Schema::object({
      {"id", Schema::string().required()},
      {"type", Schema::enumString({"function"}).required()},
      {"function", Schema::object({
                                      {"name", Schema::string().required()},
                                      {"arguments", Schema::string().offloadable().required()},
                                  })
                       .allowUnknownFields(true)
                       .required()},
  });
  return schema;
}

const Schema& chatMessageSchema() {
  static const Schema schema = Schema::object({
      {"role", Schema::enumString({"system", "user", "assistant", "tool", "function", "developer"})
                   .required()},
      {"content",
       Schema::oneOf({
           Schema::string().offloadable(),
           Schema::array(Schema::object({
               {"type", Schema::string().required()},
               {"text", Schema::string().offloadable()},
               {"image_url", Schema::object({
                                                {"url", Schema::string().offloadable().required()},
                                                {"detail", Schema::string().nullable()},
                                            })
                                 .allowUnknownFields(true)},
           })),
           Schema::null(),
       })},
      {"name", Schema::string().nullable()},
      {"tool_call_id", Schema::string().nullable()},
      {"tool_calls", Schema::array(toolCallSchema()).nullable()},
  });
  return schema;
}

const Schema& toolSchema() {
  static const Schema schema = Schema::object({
      {"type", Schema::enumString({"function"}).required()},
      {"function", Schema::object({
                                      {"name", Schema::string().required()},
                                      {"description", Schema::string().offloadable().nullable()},
                                      {"parameters", Schema::object({}).allowUnknownFields(true)},
                                      {"strict", Schema::boolean().nullable()},
                                  })
                       .allowUnknownFields(true)
                       .required()},
  });
  return schema;
}

PayloadSchema createPayloadSchema() {
  return PayloadSchema{
      /*request_schema=*/RequestSchema{
          Schema::object({
              {"model", Schema::string().required()},
              {"messages", Schema::array(chatMessageSchema()).min(1).required()},
              {"temperature", Schema::number().range(0.0, 2.0).nullable()},
              {"top_p", Schema::number().range(0.0, 1.0).nullable()},
              {"n", Schema::integer().min(1).nullable()},
              {"stream", Schema::boolean().nullable()},
              {"stop", Schema::oneOf({
                           Schema::string(),
                           Schema::array(Schema::string()),
                       }).nullable()},
              {"max_tokens", Schema::integer().min(0).nullable()},
              {"max_completion_tokens", Schema::integer().min(0).nullable()},
              {"presence_penalty", Schema::number().range(-2.0, 2.0).nullable()},
              {"frequency_penalty", Schema::number().range(-2.0, 2.0).nullable()},
              {"logit_bias", Schema::object({}).allowUnknownFields(true).nullable()},
              {"user", Schema::string().nullable()},
              {"tools", Schema::array(toolSchema()).nullable()},
              {"tool_choice", Schema::oneOf({
                                  Schema::string(),
                                  Schema::object({
                                      {"type", Schema::enumString({"function"}).required()},
                                      {"function", Schema::object({
                                                       {"name", Schema::string().required()},
                                                   })
                                                       .allowUnknownFields(true)
                                                       .required()},
                                  }),
                              }).nullable()},
              {"response_format", Schema::object({
                                      {"type", Schema::enumString(
                                                   {"text", "json_object", "json_schema"})
                                                   .required()},
                                      {"json_schema", Schema::object({}).allowUnknownFields(true)},
                                  })
                                      .allowUnknownFields(true)
                                      .nullable()},
              {"seed", Schema::integer().nullable()},
              {"service_tier", Schema::string().nullable()},
          })
              .allowUnknownFields(true),
          /*streamable_field_order=*/{
              "messages[].content",
              "messages[].content[].text",
              "messages[].content[].image_url.url",
              "messages[].tool_calls[].function.arguments",
              "tools[].function.description",
          }},
      /*response_schema=*/ResponseSchema{}};
}

} // namespace OpenAI
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
