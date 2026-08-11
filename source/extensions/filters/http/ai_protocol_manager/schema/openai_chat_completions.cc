#include "source/extensions/filters/http/ai_protocol_manager/schema/openai_chat_completions.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace OpenAi {

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
      {"content", Schema::oneOf({
                                    Schema::string().offloadable(),
                                    Schema::array(Schema::object({
                                        {"type", Schema::string().required()},
                                        {"text", Schema::string().offloadable().optional()},
                                        {"image_url",
                                         Schema::object({
                                                            {"url", Schema::string().required()},
                                                            {"detail", Schema::string().optional()},
                                                        })
                                             .allowUnknownFields(true)
                                             .optional()},
                                    })),
                                    Schema::null(),
                                })
                      .optional()},
      {"name", Schema::string().optional()},
      {"tool_call_id", Schema::string().optional()},
      {"tool_calls", Schema::array(toolCallSchema()).optional()},
  });
  return schema;
}

const Schema& toolSchema() {
  static const Schema schema = Schema::object({
      {"type", Schema::enumString({"function"}).required()},
      {"function",
       Schema::object({
                          {"name", Schema::string().required()},
                          {"description", Schema::string().offloadable().optional()},
                          {"parameters", Schema::object({}).allowUnknownFields(true).optional()},
                          {"strict", Schema::boolean().optional()},
                      })
           .allowUnknownFields(true)
           .required()},
  });
  return schema;
}

PayloadSchema createPayloadSchema() {
  return PayloadSchema{
      /*request_schema=*/RequestSchema{
          Schema::object(
              {
                  {"model", Schema::string().required()},
                  {"messages", Schema::array(chatMessageSchema()).min(1).required()},
                  {"temperature", Schema::number().range(0.0, 2.0).optional()},
                  {"top_p", Schema::number().range(0.0, 1.0).optional()},
                  {"n", Schema::integer().min(1).optional()},
                  {"stream", Schema::boolean().optional()},
                  {"stop", Schema::oneOf({
                                             Schema::string(),
                                             Schema::array(Schema::string()),
                                         })
                               .optional()},
                  {"max_tokens", Schema::integer().min(0).optional()},
                  {"max_completion_tokens", Schema::integer().min(0).optional()},
                  {"presence_penalty", Schema::number().range(-2.0, 2.0).optional()},
                  {"frequency_penalty", Schema::number().range(-2.0, 2.0).optional()},
                  {"logit_bias", Schema::object({}).allowUnknownFields(true).optional()},
                  {"user", Schema::string().optional()},
                  {"tools", Schema::array(toolSchema()).optional()},
                  {"tool_choice",
                   Schema::oneOf({
                                     Schema::string(),
                                     Schema::object({
                                         {"type", Schema::enumString({"function"}).required()},
                                         {"function",
                                          Schema::object({
                                                             {"name", Schema::string().required()},
                                                         })
                                              .allowUnknownFields(true)
                                              .required()},
                                     }),
                                 })
                       .optional()},
                  {"response_format",
                   Schema::object(
                       {
                           {"type",
                            Schema::enumString({"text", "json_object", "json_schema"}).required()},
                           {"json_schema", Schema::object({}).allowUnknownFields(true).optional()},
                       })
                       .allowUnknownFields(true)
                       .optional()},
                  {"seed", Schema::integer().optional()},
                  {"service_tier", Schema::string().optional()},
              })
              .allowUnknownFields(true)},
      /*response_schema=*/ResponseSchema{}};
}

} // namespace OpenAi
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
