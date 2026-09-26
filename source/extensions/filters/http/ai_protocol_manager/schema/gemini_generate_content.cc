#include "source/extensions/filters/http/ai_protocol_manager/schema/gemini_generate_content.h"

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Gemini {

// Being proto-generated, Gemini takes every field in more than one form
// (https://protobuf.dev/programming-guides/json/): numbers and enums each have two, so both
// take a `oneOf`; null is valid anywhere, so optional fields are nullable and array elements
// are not; and every multi-word name has a JSON and a proto spelling, so properties declare
// their `snake_case` alias inline. Bounds appear only where Gemini's proto carries a
// validator predicate. `model` and streaming are URL parameters, so not here.

namespace {

// The string form of a number still has to parse as one. `SimpleAtod` also accepts the
// "NaN" and "Infinity" spellings ProtoJSON permits for floating point fields.
absl::Status validateNumericString(const nlohmann::json& value) {
  if (!value.is_string()) {
    return absl::OkStatus();
  }
  const std::string& text = value.get_ref<const std::string&>();
  double parsed = 0;
  if (!absl::SimpleAtod(text, &parsed)) {
    return absl::InvalidArgumentError(absl::StrCat("expected a number, got '", text, "'"));
  }
  return absl::OkStatus();
}

} // namespace

// A numeric field: the number itself, or its decimal string form.
Schema numberOrString() {
  return Schema::oneOf({
      Schema::number(),
      Schema::string().customValidator(validateNumericString),
  });
}

// The range binds only to the number branch, so numeric strings go unchecked, and inclusive
// bounds are looser than exclusive ones. Both let a bad value reach Gemini rather than
// reject a good one, which is the safe direction.
Schema numberOrString(double min, double max) {
  return Schema::oneOf({
      Schema::number().range(min, max),
      Schema::string().customValidator(validateNumericString),
  });
}

// An enum field: the symbolic name or its integer. Names go unchecked because each value has
// several spellings: `serviceTier` takes `SERVICE_TIER_STANDARD` and `standard` alike.
Schema enumNameOrNumber() { return Schema::oneOf({Schema::string(), Schema::number()}); }

Schema asNullable(const Schema& schema) {
  Schema copy = schema;
  copy.nullable();
  return copy;
}

// A single Part. The data members form a proto `oneof`, so each is declared optional and
// `atMostOneOf` enforces that at most one is set. Zero is allowed: a Part may carry only
// `thought` or `partMetadata`.
const Schema& partSchema() {
  static const Schema schema =
      Schema::object(
          {
              {"text", Schema::string().offloadable().nullable()},
              // `data` is `bytes` on the wire: base64, routinely megabytes.
              {"inlineData",
               Schema::object({
                                  {"mimeType", Schema::string().nullable(), {"mime_type"}},
                                  {"data", Schema::string().offloadable().nullable()},
                              })
                   .nullable(),
               {"inline_data"}},
              {"functionCall",
               Schema::object({
                                  {"id", Schema::string().nullable()},
                                  {"name", Schema::string().required()},
                                  {"args", Schema::object({}).nullable()},
                              })
                   .nullable(),
               {"function_call"}},
              {"functionResponse",
               Schema::object({
                                  {"id", Schema::string().nullable()},
                                  {"name", Schema::string().required()},
                                  {"response", Schema::object({}).required()},
                                  // Left opaque: a FunctionResponsePart carries its own
                                  // inline blob, and `any` accepts an offloaded value at
                                  // any depth.
                                  {"parts", Schema::array(Schema::any()).nullable()},
                                  {"willContinue", Schema::boolean().nullable(), {"will_continue"}},
                                  {"scheduling", enumNameOrNumber().nullable()},
                              })
                   .nullable(),
               {"function_response"}},
              {"fileData",
               Schema::object(
                   {
                       {"mimeType", Schema::string().nullable(), {"mime_type"}},
                       // `fileUri` can exceed the inline threshold (e.g. long pre-signed URLs),
                       // so it is marked offloadable; revisit if a tighter upper bound is chosen.
                       {"fileUri", Schema::string().offloadable().required(), {"file_uri"}},
                   })
                   .nullable(),
               {"file_data"}},
              {"executableCode",
               Schema::object({
                                  {"language", enumNameOrNumber().required()},
                                  {"code", Schema::string().offloadable().required()},
                              })
                   .nullable(),
               {"executable_code"}},
              {"codeExecutionResult",
               Schema::object({
                                  {"outcome", enumNameOrNumber().required()},
                                  {"output", Schema::string().offloadable().nullable()},
                              })
                   .nullable(),
               {"code_execution_result"}},
              // The offsets are `Duration`, which ProtoJSON renders as a string like "3.5s".
              {"videoMetadata",
               Schema::object({
                                  {"startOffset", Schema::string().nullable(), {"start_offset"}},
                                  {"endOffset", Schema::string().nullable(), {"end_offset"}},
                                  {"fps", numberOrString().nullable()},
                              })
                   .nullable(),
               {"video_metadata"}},
              {"thought", Schema::boolean().nullable()},
              // An opaque signature, `bytes` on the wire, with no documented size bound.
              {"thoughtSignature",
               Schema::string().offloadable().nullable(),
               {"thought_signature"}},
              {"partMetadata", Schema::object({}).nullable(), {"part_metadata"}},
          })
          .atMostOneOf({"text", "inlineData", "functionCall", "functionResponse", "fileData",
                        "executableCode", "codeExecutionResult"});
  return schema;
}

const Schema& contentSchema() {
  static const Schema schema = Schema::object({
      // Not marked required: the proto places no field behavior on `parts`.
      {"parts", Schema::array(partSchema()).nullable()},
      // A plain string in the proto, not an enum, so the closed set in its doc comment
      // ("Must be either 'user' or 'model'.") is not enforced. Gemini's own docs use role
      // "function" for function-response turns, so listing values here would reject requests
      // the API accepts.
      {"role", Schema::string().nullable()},
  });
  return schema;
}

const Schema& functionDeclarationSchema() {
  static const Schema schema =
      Schema::object({
                         {"name", Schema::string().required()},
                         // The Gemini Developer API documents this as required, but Vertex
                         // documents it as optional, so requiring it would reject valid Vertex
                         // traffic. It is the one tool field large enough to be worth offloading.
                         {"description", Schema::string().offloadable().nullable()},
                         // Contents unchecked, as for `responseSchema` below. Each pair is mutually
                         // exclusive, checked below.
                         {"parameters", Schema::object({}).nullable()},
                         {"parametersJsonSchema", Schema::any(), {"parameters_json_schema"}},
                         {"response", Schema::object({}).nullable()},
                         {"responseJsonSchema", Schema::any(), {"response_json_schema"}},
                         {"behavior", enumNameOrNumber().nullable()},
                     })
          .atMostOneOf({"parameters", "parametersJsonSchema"})
          .atMostOneOf({"response", "responseJsonSchema"});
  return schema;
}

// Most of the built-in tools are empty marker messages, so there is nothing to assert
// beyond their being objects.
const Schema& toolSchema() {
  static const Schema schema = Schema::object({
      {"functionDeclarations",
       Schema::array(functionDeclarationSchema()).nullable(),
       {"function_declarations"}},
      {"googleSearch", Schema::object({}).nullable(), {"google_search"}},
      {"googleSearchRetrieval", Schema::object({}).nullable(), {"google_search_retrieval"}},
      {"codeExecution", Schema::object({}).nullable(), {"code_execution"}},
      {"computerUse", Schema::object({}).nullable(), {"computer_use"}},
      {"urlContext", Schema::object({}).nullable(), {"url_context"}},
      {"fileSearch", Schema::object({}).nullable(), {"file_search"}},
      {"googleMaps", Schema::object({}).nullable(), {"google_maps"}},
  });
  return schema;
}

// Gemini's equivalent of the OpenAI `tool_choice` field.
const Schema& toolConfigSchema() {
  static const Schema schema = Schema::object({
      {"functionCallingConfig",
       Schema::object({
                          {"mode", enumNameOrNumber().nullable()},
                          {"allowedFunctionNames",
                           Schema::array(Schema::string()).nullable(),
                           {"allowed_function_names"}},
                      })
           .nullable(),
       {"function_calling_config"}},
      {"retrievalConfig", Schema::object({}).nullable(), {"retrieval_config"}},
  });
  return schema;
}

// `category` and `threshold` are both REQUIRED in the proto, which makes this the most
// substantial structural check the request surface offers.
const Schema& safetySettingSchema() {
  static const Schema schema = Schema::object({
      {"category", enumNameOrNumber().required()},
      {"threshold", enumNameOrNumber().required()},
  });
  return schema;
}

const Schema& generationConfigSchema() {
  static const Schema schema =
      Schema::object(
          {
              {"candidateCount", numberOrString(1, 8).nullable(), {"candidate_count"}},
              {"stopSequences",
               Schema::array(Schema::string()).max(5).nullable(),
               {"stop_sequences"}},
              {"maxOutputTokens", numberOrString().nullable(), {"max_output_tokens"}},
              {"temperature", numberOrString(0.0, 2.0).nullable()},
              {"topP", numberOrString(0.0, 1.0).nullable(), {"top_p"}},
              {"topK", numberOrString().nullable(), {"top_k"}},
              {"seed", numberOrString().nullable()},
              {"responseMimeType", Schema::string().nullable(), {"response_mime_type"}},
              // Contents unchecked: an OpenAPI schema nests itself, and a schema here cannot
              // refer to itself. `responseJsonSchema` may be any JSON type. The two are
              // mutually exclusive, checked below.
              {"responseSchema", Schema::object({}).nullable(), {"response_schema"}},
              {"responseJsonSchema", Schema::any(), {"response_json_schema"}},
              {"presencePenalty", numberOrString(-2.0, 2.0).nullable(), {"presence_penalty"}},
              {"frequencyPenalty", numberOrString(-2.0, 2.0).nullable(), {"frequency_penalty"}},
              {"responseLogprobs", Schema::boolean().nullable(), {"response_logprobs"}},
              {"logprobs", numberOrString(0, 20).nullable()},
              {"enableEnhancedCivicAnswers",
               Schema::boolean().nullable(),
               {"enable_enhanced_civic_answers"}},
              {"responseModalities",
               Schema::array(enumNameOrNumber()).nullable(),
               {"response_modalities"}},
              {"speechConfig", Schema::object({}).nullable(), {"speech_config"}},
              {"thinkingConfig",
               Schema::object(
                   {
                       {"includeThoughts", Schema::boolean().nullable(), {"include_thoughts"}},
                       // -1 selects dynamic thinking, so the range starts below 0.
                       {"thinkingBudget",
                        numberOrString(-1, 65535).nullable(),
                        {"thinking_budget"}},
                       // Says the same thing as `thinkingBudget` in words rather
                       // than tokens, so only one of the two may be set.
                       {"thinkingLevel", enumNameOrNumber().nullable(), {"thinking_level"}},
                   })
                   .atMostOneOf({"thinkingBudget", "thinkingLevel"})
                   .nullable(),
               {"thinking_config"}},
              {"imageConfig", Schema::object({}).nullable(), {"image_config"}},
              {"mediaResolution", enumNameOrNumber().nullable(), {"media_resolution"}},
          })
          .atMostOneOf({"responseSchema", "responseJsonSchema"});
  return schema;
}

PayloadSchema createPayloadSchema() {
  return PayloadSchema{
      /*request_schema=*/RequestSchema{
          Schema::object(
              {
                  // Not nullable: a null would unset the one field the API requires.
                  {"contents", Schema::array(contentSchema()).min(1).required()},
                  {"systemInstruction", asNullable(contentSchema()), {"system_instruction"}},
                  {"tools", Schema::array(toolSchema()).nullable()},
                  {"toolConfig", asNullable(toolConfigSchema()), {"tool_config"}},
                  {"safetySettings",
                   Schema::array(safetySettingSchema()).nullable(),
                   {"safety_settings"}},
                  {"generationConfig", asNullable(generationConfigSchema()), {"generation_config"}},
                  // A resource name, e.g. `cachedContents/1234`.
                  {"cachedContent", Schema::string().nullable(), {"cached_content"}},
                  {"serviceTier", enumNameOrNumber().nullable(), {"service_tier"}},
                  {"store", Schema::boolean().nullable()},
              })
              // Gemini names the unknown field in its own error, which beats anything we
              // could say here. Rejecting instead would mean every field it gains fails at
              // this filter until someone edits this list.
              .allowUnknownFields(true),
          /*streamable_field_order=*/
          {
              "contents[].parts[].text",
              "contents[].parts[].inlineData.data",
              "contents[].parts[].fileData.fileUri",
              "contents[].parts[].executableCode.code",
              "contents[].parts[].codeExecutionResult.output",
              "contents[].parts[].thoughtSignature",
              "systemInstruction.parts[].text",
              "systemInstruction.parts[].inlineData.data",
              "systemInstruction.parts[].fileData.fileUri",
              "systemInstruction.parts[].executableCode.code",
              "systemInstruction.parts[].codeExecutionResult.output",
              "systemInstruction.parts[].thoughtSignature",
              "tools[].functionDeclarations[].description",
          }},
      /*response_schema=*/ResponseSchema{}};
}

} // namespace Gemini
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
