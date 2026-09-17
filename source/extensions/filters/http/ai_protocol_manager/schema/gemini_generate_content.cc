#include "source/extensions/filters/http/ai_protocol_manager/schema/gemini_generate_content.h"

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Gemini {

// Being proto-generated, Gemini takes every field in more than one form
// (https://protobuf.dev/programming-guides/json/): numbers and enums each have two, so both
// take a `oneOf`; null is valid anywhere, so optional fields are nullable and array elements
// are not; and every name has a JSON and a proto spelling, so the properties below are keyed
// on the JSON one and `isSet()` resolves both. Bounds appear only where Gemini's proto
// carries a validator predicate. `model` and streaming are URL parameters, so not here.

namespace {

// The proto name for a JSON field name: `inlineData` becomes `inline_data`. A name that is
// already a single lowercase word is returned unchanged, and both spellings are then the
// same string.
std::string protoNameOf(const char* json_name) {
  std::string proto_name;
  for (const char* c = json_name; *c != '\0'; ++c) {
    if (absl::ascii_isupper(*c)) {
      proto_name.push_back('_');
      proto_name.push_back(absl::ascii_tolower(*c));
    } else {
      proto_name.push_back(*c);
    }
  }
  return proto_name;
}

bool isSetUnder(const nlohmann::json& object, const std::string& name) {
  const auto it = object.find(name);
  return it != object.end() && !it->is_null();
}

// Returns true if `object` has `name` set to something other than null, under either
// spelling ProtoJSON accepts for it. A null does not count as present: ProtoJSON treats it
// as leaving the field unset.
bool isSet(const nlohmann::json& object, const char* name) {
  if (isSetUnder(object, name)) {
    return true;
  }
  const std::string proto_name = protoNameOf(name);
  return proto_name != name && isSetUnder(object, proto_name);
}

absl::Status rejectIfBothSet(const nlohmann::json& object, const char* first, const char* second) {
  if (isSet(object, first) && isSet(object, second)) {
    return absl::InvalidArgumentError(
        absl::StrCat("'", first, "' and '", second, "' are mutually exclusive"));
  }
  return absl::OkStatus();
}

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

absl::Status validateFunctionDeclarationExclusions(const nlohmann::json& declaration) {
  absl::Status status = rejectIfBothSet(declaration, "parameters", "parametersJsonSchema");
  if (!status.ok()) {
    return status;
  }
  return rejectIfBothSet(declaration, "response", "responseJsonSchema");
}

absl::Status validateGenerationConfigExclusions(const nlohmann::json& config) {
  return rejectIfBothSet(config, "responseSchema", "responseJsonSchema");
}

absl::Status validateThinkingConfigExclusions(const nlohmann::json& config) {
  return rejectIfBothSet(config, "thinkingBudget", "thinkingLevel");
}

// `fileUri` is required, but `required()` looks for that one spelling and would reject a
// request that sends `file_uri`. Checking presence here accepts both spellings.
absl::Status validateFileDataRequired(const nlohmann::json& file_data) {
  if (!isSet(file_data, "fileUri")) {
    return absl::InvalidArgumentError("missing required field: 'fileUri'");
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

absl::Status validatePartDataOneOf(const nlohmann::json& part) {
  static const char* kDataMembers[] = {
      "text",     "inlineData",     "functionCall",       "functionResponse",
      "fileData", "executableCode", "codeExecutionResult"};

  std::vector<std::string> present;
  for (const char* member : kDataMembers) {
    if (isSet(part, member)) {
      present.emplace_back(member);
    }
  }
  if (present.size() > 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "a Part may set at most one of its data members, got: ", absl::StrJoin(present, ", ")));
  }
  return absl::OkStatus();
}

// A single Part. The data members form a proto `oneof`, so each is declared optional and a
// custom validator enforces that at most one is set. Zero is allowed: a Part may carry only
// `thought` or `partMetadata`.
const Schema& partSchema() {
  static const Schema schema =
      Schema::object(
          {
              {"text", Schema::string().offloadable().nullable()},
              // `data` is `bytes` on the wire: base64, routinely megabytes.
              {"inlineData", Schema::object({
                                                {"mimeType", Schema::string().nullable()},
                                                {"data", Schema::string().offloadable().nullable()},
                                            })
                                 .nullable()},
              {"functionCall", Schema::object({
                                                  {"id", Schema::string().nullable()},
                                                  {"name", Schema::string().required()},
                                                  {"args", Schema::object({}).nullable()},
                                              })
                                   .nullable()},
              {"functionResponse",
               Schema::object({
                                  {"id", Schema::string().nullable()},
                                  {"name", Schema::string().required()},
                                  {"response", Schema::object({}).required()},
                                  // Left opaque: a FunctionResponsePart carries its own
                                  // inline blob, and `any` accepts an offloaded value at
                                  // any depth.
                                  {"parts", Schema::array(Schema::any()).nullable()},
                                  {"willContinue", Schema::boolean().nullable()},
                                  {"scheduling", enumNameOrNumber().nullable()},
                              })
                   .nullable()},
              // `fileUri` is a URI, comfortably below the offload threshold. It is required;
              // the check sits on the object so it can accept `file_uri` too.
              {"fileData", Schema::object({
                                              {"mimeType", Schema::string().nullable()},
                                              {"fileUri", Schema::string().nullable()},
                                          })
                               .customValidator(validateFileDataRequired)
                               .nullable()},
              {"executableCode",
               Schema::object({
                                  {"language", enumNameOrNumber().required()},
                                  {"code", Schema::string().offloadable().required()},
                              })
                   .nullable()},
              {"codeExecutionResult",
               Schema::object({
                                  {"outcome", enumNameOrNumber().required()},
                                  {"output", Schema::string().offloadable().nullable()},
                              })
                   .nullable()},
              // The offsets are `Duration`, which ProtoJSON renders as a string like "3.5s".
              {"videoMetadata", Schema::object({
                                                   {"startOffset", Schema::string().nullable()},
                                                   {"endOffset", Schema::string().nullable()},
                                                   {"fps", numberOrString().nullable()},
                                               })
                                    .nullable()},
              {"thought", Schema::boolean().nullable()},
              // An opaque signature, `bytes` on the wire, with no documented size bound.
              {"thoughtSignature", Schema::string().offloadable().nullable()},
              {"partMetadata", Schema::object({}).nullable()},
          })
          .customValidator(validatePartDataOneOf);
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
                         // Required by the proto, and the one tool field large enough to be worth
                         // offloading.
                         {"description", Schema::string().offloadable().required()},
                         // Contents unchecked, as for `responseSchema` below. Each pair is mutually
                         // exclusive, checked below.
                         {"parameters", Schema::object({}).nullable()},
                         {"parametersJsonSchema", Schema::any()},
                         {"response", Schema::object({}).nullable()},
                         {"responseJsonSchema", Schema::any()},
                         {"behavior", enumNameOrNumber().nullable()},
                     })
          .customValidator(validateFunctionDeclarationExclusions);
  return schema;
}

// Most of the built-in tools are empty marker messages, so there is nothing to assert
// beyond their being objects.
const Schema& toolSchema() {
  static const Schema schema = Schema::object({
      {"functionDeclarations", Schema::array(functionDeclarationSchema()).nullable()},
      {"googleSearch", Schema::object({}).nullable()},
      {"googleSearchRetrieval", Schema::object({}).nullable()},
      {"codeExecution", Schema::object({}).nullable()},
      {"computerUse", Schema::object({}).nullable()},
      {"urlContext", Schema::object({}).nullable()},
      {"fileSearch", Schema::object({}).nullable()},
      {"googleMaps", Schema::object({}).nullable()},
  });
  return schema;
}

// Gemini's equivalent of the OpenAI `tool_choice` field.
const Schema& toolConfigSchema() {
  static const Schema schema = Schema::object({
      {"functionCallingConfig",
       Schema::object({
                          {"mode", enumNameOrNumber().nullable()},
                          {"allowedFunctionNames", Schema::array(Schema::string()).nullable()},
                      })
           .nullable()},
      {"retrievalConfig", Schema::object({}).nullable()},
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
              {"candidateCount", numberOrString(1, 8).nullable()},
              {"stopSequences", Schema::array(Schema::string()).max(5).nullable()},
              {"maxOutputTokens", numberOrString().nullable()},
              {"temperature", numberOrString(0.0, 2.0).nullable()},
              {"topP", numberOrString(0.0, 1.0).nullable()},
              {"topK", numberOrString().nullable()},
              {"seed", numberOrString().nullable()},
              {"responseMimeType", Schema::string().nullable()},
              // Contents unchecked: an OpenAPI schema nests itself, and a schema here cannot
              // refer to itself. `responseJsonSchema` may be any JSON type. The two are
              // mutually exclusive, checked below.
              {"responseSchema", Schema::object({}).nullable()},
              {"responseJsonSchema", Schema::any()},
              {"presencePenalty", numberOrString(-2.0, 2.0).nullable()},
              {"frequencyPenalty", numberOrString(-2.0, 2.0).nullable()},
              {"responseLogprobs", Schema::boolean().nullable()},
              {"logprobs", numberOrString(0, 20).nullable()},
              {"enableEnhancedCivicAnswers", Schema::boolean().nullable()},
              {"responseModalities", Schema::array(enumNameOrNumber()).nullable()},
              {"speechConfig", Schema::object({}).nullable()},
              {"thinkingConfig",
               Schema::object({
                                  {"includeThoughts", Schema::boolean().nullable()},
                                  // -1 selects dynamic thinking, so the range starts below 0.
                                  {"thinkingBudget", numberOrString(-1, 65535).nullable()},
                                  // Says the same thing as `thinkingBudget` in words rather
                                  // than tokens, so only one of the two may be set.
                                  {"thinkingLevel", enumNameOrNumber().nullable()},
                              })
                   .customValidator(validateThinkingConfigExclusions)
                   .nullable()},
              {"imageConfig", Schema::object({}).nullable()},
              {"mediaResolution", enumNameOrNumber().nullable()},
          })
          .customValidator(validateGenerationConfigExclusions);
  return schema;
}

PayloadSchema createPayloadSchema() {
  return PayloadSchema{
      /*request_schema=*/RequestSchema{
          Schema::object({
                             // Not nullable: a null would unset the one field the API requires.
                             {"contents", Schema::array(contentSchema()).min(1).required()},
                             {"systemInstruction", asNullable(contentSchema())},
                             {"tools", Schema::array(toolSchema()).nullable()},
                             {"toolConfig", asNullable(toolConfigSchema())},
                             {"safetySettings", Schema::array(safetySettingSchema()).nullable()},
                             {"generationConfig", asNullable(generationConfigSchema())},
                             // A resource name, e.g. `cachedContents/1234`.
                             {"cachedContent", Schema::string().nullable()},
                             {"serviceTier", enumNameOrNumber().nullable()},
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
              "contents[].parts[].executableCode.code",
              "contents[].parts[].codeExecutionResult.output",
              "contents[].parts[].thoughtSignature",
              "systemInstruction.parts[].text",
              "systemInstruction.parts[].inlineData.data",
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
