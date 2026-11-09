#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema/gemini_generate_content.h"

#include "test/test_common/status_utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace Gemini {
namespace {

using StatusHelpers::IsOk;
using StatusHelpers::StatusCodeIs;

TEST(GeminiGenerateContentTest, StandardValidPayload) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json valid_req = {
      {"contents", nlohmann::json::array({
                       {{"role", "user"}, {"parts", nlohmann::json::array({{{"text", "Hello!"}}})}},
                   })},
      {"systemInstruction", {{"parts", nlohmann::json::array({{{"text", "Be concise."}}})}}},
      {"generationConfig", {{"temperature", 0.7}, {"maxOutputTokens", 100}}},
      {"serviceTier", "SERVICE_TIER_STANDARD"},
      {"store", false},
  };
  EXPECT_THAT(payload_schema.validateRequest(valid_req), IsOk());
}

// `model` is a URL path parameter and streaming is chosen by the method name, so neither
// appears in the body. A client that sends them anyway is not rejected: they arrive as
// unknown fields.
TEST(GeminiGenerateContentTest, ModelAndStreamAreNotBodyFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json req = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"model", "models/gemini-2.0-flash"},
      {"stream", true},
  };
  EXPECT_THAT(payload_schema.validateRequest(req), IsOk());
}

// proto3 maps every numeric scalar to "either a number or its decimal string", so both
// encodings have to survive validation.
TEST(GeminiGenerateContentTest, NumericFieldsAcceptNumberOrString) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json numeric_req = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig",
       {
           {"temperature", 0.7},
           {"topP", 0.95},
           {"topK", 40},
           {"candidateCount", 1},
           {"maxOutputTokens", 2048},
           {"seed", 42},
           {"presencePenalty", -0.5},
           {"frequencyPenalty", 1.5},
           {"logprobs", 5},
       }},
  };
  EXPECT_THAT(payload_schema.validateRequest(numeric_req), IsOk());

  nlohmann::json stringified_req = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig",
       {
           {"temperature", "0.7"},
           {"topP", "0.95"},
           {"topK", "40"},
           {"candidateCount", "1"},
           {"maxOutputTokens", "2048"},
           {"seed", "42"},
           {"presencePenalty", "-0.5"},
           {"frequencyPenalty", "1.5"},
           {"logprobs", "5"},
       }},
  };
  EXPECT_THAT(payload_schema.validateRequest(stringified_req), IsOk());
}

// Bounds are declared only where Gemini's proto carries a validator predicate, so these are
// rejections the upstream API would make too.
TEST(GeminiGenerateContentTest, NumericFieldsRespectVerifiedBounds) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_config = [](nlohmann::json config) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                     })},
        {"generationConfig", std::move(config)},
    };
  };

  // `temperature` is [0.0, 2.0], the penalties [-2.0, 2.0), `candidateCount` [1, 8],
  // `topP` [0.0, 1.0], `logprobs` [0, 20] and `thinkingBudget` [-1, 65535].
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"temperature", 5.0}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"presencePenalty", -10.0}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"candidateCount", 0}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"topP", 1.5}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"logprobs", 50}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(
                  with_config({{"thinkingConfig", {{"thinkingBudget", 99999}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(
                  with_config({{"stopSequences", {"a", "b", "c", "d", "e", "f"}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // -1 is dynamic thinking, and the penalties genuinely accept negatives.
  EXPECT_THAT(payload_schema.validateRequest(with_config(
                  {{"thinkingConfig", {{"thinkingBudget", -1}}}, {"presencePenalty", -1.5}})),
              IsOk());

  // Two deliberate gaps. A `oneOf` checks each candidate separately, so the range never
  // reaches the string form; and the declared bounds are inclusive where Gemini's are not.
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"temperature", "5.0"}})), IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_config({{"presencePenalty", 2.0}})), IsOk());
}

// Every Gemini enum accepts the symbolic name or the equivalent integer, so neither form
// may be rejected.
TEST(GeminiGenerateContentTest, EnumsAcceptNameOrInteger) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json named_enums = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"safetySettings",
       nlohmann::json::array({
           {{"category", "HARM_CATEGORY_DANGEROUS_CONTENT"}, {"threshold", "BLOCK_ONLY_HIGH"}},
       })},
      {"generationConfig", {{"mediaResolution", "MEDIA_RESOLUTION_MEDIUM"}}},
      {"toolConfig", {{"functionCallingConfig", {{"mode", "ANY"}}}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(named_enums), IsOk());

  nlohmann::json integer_enums = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"safetySettings", nlohmann::json::array({
                             {{"category", 7}, {"threshold", 2}},
                         })},
      {"generationConfig", {{"mediaResolution", 2}}},
      {"toolConfig", {{"functionCallingConfig", {{"mode", 2}}}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(integer_enums), IsOk());
}

// The `Part.data` members form a proto oneof, so setting two of them is a request Gemini
// would reject.
TEST(GeminiGenerateContentTest, PartRejectsMultipleDataMembers) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_part = [](nlohmann::json part) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({part})}},
                     })},
    };
  };

  EXPECT_THAT(payload_schema.validateRequest(
                  with_part({{"text", "hi"}, {"functionCall", {{"name", "f"}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(payload_schema.validateRequest(
                  with_part({{"inlineData", {{"data", "aGk="}}},
                             {"fileData", {{"fileUri", "https://example.com/a"}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // A null data member is unset, so it does not occupy the oneof.
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"text", "hi"}, {"functionCall", nullptr}})),
      IsOk());

  // Zero data members is legal: a Part may carry only `thought`.
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"thought", true}})), IsOk());

  // `thought`, `thoughtSignature` and `partMetadata` sit outside the oneof.
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"text", "hi"},
                                                        {"thought", true},
                                                        {"thoughtSignature", "c2ln"},
                                                        {"partMetadata", {{"k", "v"}}}})),
              IsOk());
}

// The string form of a number still has to parse as one. Enum names are checked by a
// different helper, so they are unaffected.
TEST(GeminiGenerateContentTest, NumericStringsMustParseAsNumbers) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_generation_config = [](nlohmann::json config) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                     })},
        {"generationConfig", config},
    };
  };

  EXPECT_THAT(payload_schema.validateRequest(with_generation_config({{"temperature", "hello"}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_generation_config({{"maxOutputTokens", ""}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Forms ProtoJSON permits for a floating point field.
  EXPECT_THAT(payload_schema.validateRequest(with_generation_config({{"temperature", "0.7"}})),
              IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_generation_config({{"temperature", "-1e3"}})),
              IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_generation_config({{"temperature", "NaN"}})),
              IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_generation_config({{"temperature", "Infinity"}})),
              IsOk());

  // An enum name is not a number, and must still be accepted.
  EXPECT_THAT(payload_schema.validateRequest(
                  with_generation_config({{"mediaResolution", "MEDIA_RESOLUTION_LOW"}})),
              IsOk());
}

// The proto documents each of these pairs as mutually exclusive.
TEST(GeminiGenerateContentTest, MutuallyExclusiveFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json both_response_schemas = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig",
       {{"responseSchema", {{"type", "object"}}}, {"responseJsonSchema", {{"type", "object"}}}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(both_response_schemas),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json both_parameter_schemas = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"tools", nlohmann::json::array({
                    {{"functionDeclarations", nlohmann::json::array({
                                                  {{"name", "f"},
                                                   {"description", "d"},
                                                   {"parameters", {{"type", "object"}}},
                                                   {"parametersJsonSchema", {{"type", "object"}}}},
                                              })}},
                })},
  };
  EXPECT_THAT(payload_schema.validateRequest(both_parameter_schemas),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json both_thinking_controls = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig",
       {{"thinkingConfig", {{"thinkingBudget", 1024}, {"thinkingLevel", "HIGH"}}}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(both_thinking_controls),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Setting one of a pair, or nulling the other, is fine.
  nlohmann::json one_of_each = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig",
       {{"responseSchema", {{"type", "object"}}}, {"responseJsonSchema", nullptr}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(one_of_each), IsOk());
}

// An enum is "name or integer", which is exactly what `enumNameOrNumber()` describes, so
// structured values are still rejected.
TEST(GeminiGenerateContentTest, EnumsRejectStructuredValues) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json object_enum = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig", {{"mediaResolution", {{"value", 2}}}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(object_enum),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json boolean_mode = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"toolConfig", {{"functionCallingConfig", {{"mode", true}}}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(boolean_mode),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// ProtoJSON: "null is valid for any field and leaves the field unset." Every optional field
// must therefore tolerate an explicit null.
TEST(GeminiGenerateContentTest, ExplicitNullsOnOptionalFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json null_top_level = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"systemInstruction", nullptr},
      {"tools", nullptr},
      {"toolConfig", nullptr},
      {"safetySettings", nullptr},
      {"generationConfig", nullptr},
      {"cachedContent", nullptr},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_top_level), IsOk());

  nlohmann::json null_generation_config = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig",
       {
           {"temperature", nullptr},
           {"topP", nullptr},
           {"maxOutputTokens", nullptr},
           {"stopSequences", nullptr},
           {"responseMimeType", nullptr},
           {"responseSchema", nullptr},
           {"mediaResolution", nullptr},
           {"thinkingConfig", nullptr},
       }},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_generation_config), IsOk());

  nlohmann::json null_part_members = {
      {"contents",
       nlohmann::json::array({
           {{"role", nullptr},
            {"parts", nlohmann::json::array({
                          {{"text", nullptr}},
                          {{"inlineData", nullptr}},
                          {{"thought", nullptr}, {"thoughtSignature", nullptr}},
                          {{"functionCall", {{"name", "f"}, {"id", nullptr}, {"args", nullptr}}}},
                      })}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_part_members), IsOk());
}

// The two places a null must still be rejected: a required field, where the ProtoJSON "null
// leaves the field unset" rule makes it equivalent to omission, and a repeated field's elements,
// where the spec forbids null outright.
TEST(GeminiGenerateContentTest, NullRejectedWhereProtoJsonForbidsIt) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json null_element = {
      {"contents", nlohmann::json::array({nullptr})},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_element),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_required_nested = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({
                                      {{"functionCall", {{"name", nullptr}}}},
                                  })}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_required_nested),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_safety_threshold = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"safetySettings", nlohmann::json::array({
                             {{"category", "HARM_CATEGORY_HARASSMENT"}, {"threshold", nullptr}},
                         })},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_safety_threshold),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// `Content.role` is a plain proto string, not a proto enum, and `systemInstruction` reuses
// the same schema. Neither an unexpected role nor a role on `systemInstruction` may be
// rejected.
TEST(GeminiGenerateContentTest, RoleIsUnconstrained) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json roles_req = {
      {"contents", nlohmann::json::array({
                       {{"role", "user"}, {"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                       {{"role", "model"}, {"parts", nlohmann::json::array({{{"text", "Yo"}}})}},
                       {{"role", "function"}, {"parts", nlohmann::json::array({{{"text", "?"}}})}},
                   })},
      {"systemInstruction",
       {{"role", "system"}, {"parts", nlohmann::json::array({{{"text", "Be nice."}}})}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(roles_req), IsOk());
}

TEST(GeminiGenerateContentTest, AllPartKinds) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json parts_req = {
      {"contents",
       nlohmann::json::array({
           {{"role", "user"},
            {"parts",
             nlohmann::json::array({
                 {{"text", "Describe this."}},
                 {{"inlineData", {{"mimeType", "image/png"}, {"data", "aGVsbG8="}}}},
                 {{"fileData",
                   {{"mimeType", "video/mp4"}, {"fileUri", "https://example.com/v.mp4"}}}},
                 {{"functionCall",
                   {{"id", "fc_1"}, {"name", "get_weather"}, {"args", {{"city", "Paris"}}}}}},
                 {{"functionResponse",
                   {{"id", "fc_1"}, {"name", "get_weather"}, {"response", {{"tempC", 18}}}}}},
                 {{"executableCode", {{"language", "PYTHON"}, {"code", "print(1)"}}}},
                 {{"codeExecutionResult", {{"outcome", "OUTCOME_OK"}, {"output", "1\n"}}}},
                 {{"thought", true}, {"thoughtSignature", "c2ln"}},
                 {{"text", "done"}, {"partMetadata", {{"source", "notes.txt"}}}},
             })}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(parts_req), IsOk());
}

TEST(GeminiGenerateContentTest, ToolsAndToolConfig) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json tools_req = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Weather?"}}})}},
                   })},
      {"tools",
       nlohmann::json::array({
           {{"functionDeclarations", nlohmann::json::array({
                                         {{"name", "get_weather"},
                                          {"description", "Look up the weather for a city."},
                                          {"parameters", {{"type", "object"}}}},
                                     })}},
           {{"googleSearch", nlohmann::json::object()}},
           {{"codeExecution", nlohmann::json::object()}},
       })},
      {"toolConfig",
       {{"functionCallingConfig",
         {{"mode", "ANY"}, {"allowedFunctionNames", nlohmann::json::array({"get_weather"})}}}}},
      {"cachedContent", "cachedContents/1234"},
  };
  EXPECT_THAT(payload_schema.validateRequest(tools_req), IsOk());
}

TEST(GeminiGenerateContentTest, OffloadedValuesAccepted) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json offloaded_req = {
      {"contents",
       nlohmann::json::array({
           {{"parts",
             nlohmann::json::array({
                 {{"text", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 50000})}},
                 {{"inlineData",
                   {{"data",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{100, 900000})}}}},
                 {{"fileData",
                   {{"fileUri",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{150, 2000})}}}},
                 {{"executableCode",
                   {{"language", "PYTHON"},
                    {"code",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{200, 8000})}}}},
                 {{"codeExecutionResult",
                   {{"outcome", "OUTCOME_OK"},
                    {"output",
                     JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{300, 9000})}}}},
                 {{"thoughtSignature",
                   JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{400, 4000})}},
             })}},
       })},
      {"tools", nlohmann::json::array({
                    {{"functionDeclarations",
                      nlohmann::json::array({
                          {{"name", "f"},
                           {"description", JsonWithExtBuf::makeExternalRef(
                                               JsonWithExtBuf::ExternalRef{500, 3000})}},
                      })}},
                })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_req), IsOk());
}

// A string field that is not marked offloadable cannot hold an external reference. This is
// the most common way a schema change accidentally starts returning 400s.
TEST(GeminiGenerateContentTest, NonOffloadableFieldsRejectExternalRefs) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json offloaded_role = {
      {"contents",
       nlohmann::json::array({
           {{"role", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 10})},
            {"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_role),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json offloaded_mime_type = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({
                                      {{"fileData",
                                        {{"mimeType", JsonWithExtBuf::makeExternalRef(
                                                          JsonWithExtBuf::ExternalRef{0, 2000})},
                                         {"fileUri", "gs://bucket/obj"}}}},
                                  })}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(offloaded_mime_type),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// An offloaded value under a field the schema does not declare must not be rejected: an
// unknown key is never traversed, so its value is never type checked.
TEST(GeminiGenerateContentTest, OffloadedValueInUndeclaredFieldIsAccepted) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json undeclared = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"labels",
       {{"trace", JsonWithExtBuf::makeExternalRef(JsonWithExtBuf::ExternalRef{0, 5000})}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(undeclared), IsOk());
}

// Public documentation is ahead of the proto snapshot this schema was written from, so
// newer fields have to pass through untouched.
TEST(GeminiGenerateContentTest, UnknownFieldsPassThrough) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json newer_fields = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig", {{"thinkingLevel", "HIGH"}, {"serviceTier", "PRIORITY"}}},
      {"mcpServers", nlohmann::json::array()},
  };
  EXPECT_THAT(payload_schema.validateRequest(newer_fields), IsOk());
}

TEST(GeminiGenerateContentTest, MissingRequiredTopLevelFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json missing_contents = {
      {"generationConfig", {{"temperature", 0.5}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_contents),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json empty_contents = {
      {"contents", nlohmann::json::array()},
  };
  EXPECT_THAT(payload_schema.validateRequest(empty_contents),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json null_contents = {
      {"contents", nullptr},
  };
  EXPECT_THAT(payload_schema.validateRequest(null_contents),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// Each of these mirrors a `(google.api.field_behavior) = REQUIRED` in the proto.
TEST(GeminiGenerateContentTest, MissingRequiredNestedFields) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_part = [](nlohmann::json part) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({part})}},
                     })},
    };
  };

  // functionCall.name
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"functionCall", {{"id", "1"}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // functionResponse.name
  EXPECT_THAT(payload_schema.validateRequest(
                  with_part({{"functionResponse", {{"response", nlohmann::json::object()}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // functionResponse.response
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"functionResponse", {{"name", "f"}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // fileData.fileUri
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"fileData", {{"mimeType", "video/mp4"}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // executableCode.language and .code
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"executableCode", {{"code", "print(1)"}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"executableCode", {{"language", "PYTHON"}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // codeExecutionResult.outcome
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"codeExecutionResult", {{"output", "1"}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // safetySettings[].category and .threshold
  nlohmann::json missing_threshold = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"safetySettings", nlohmann::json::array({{{"category", "HARM_CATEGORY_HARASSMENT"}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_threshold),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // tools[].functionDeclarations[].name (description is optional for Vertex compatibility).
  nlohmann::json missing_function_name = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"tools", nlohmann::json::array({
                    {{"functionDeclarations", nlohmann::json::array({{{"description", "d"}}})}},
                })},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_function_name),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json missing_description = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"tools", nlohmann::json::array({
                    {{"functionDeclarations", nlohmann::json::array({{{"name", "f"}}})}},
                })},
  };
  EXPECT_THAT(payload_schema.validateRequest(missing_description), IsOk());
}

// ProtoJSON accepts a field under its JSON name or its proto name, so a payload spelled the
// proto way is validated identically to the camelCase one: valid values pass, and invalid
// values or missing required fields under snake_case (including parent objects) are rejected.
TEST(GeminiGenerateContentTest, ProtoFieldNamesAccepted) {
  PayloadSchema payload_schema = createPayloadSchema();

  nlohmann::json proto_named = {
      {"contents", nlohmann::json::array({
                       {{"role", "user"}, {"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"system_instruction", {{"parts", nlohmann::json::array({{{"text", "Be concise."}}})}}},
      {"generation_config", {{"temperature", 0.7}, {"max_output_tokens", 100}}},
      {"safety_settings", nlohmann::json::array({{{"category", "HARM_CATEGORY_HARASSMENT"},
                                                  {"threshold", "BLOCK_NONE"}}})},
      {"cached_content", "cachedContents/1234"},
  };
  EXPECT_THAT(payload_schema.validateRequest(proto_named), IsOk());

  // Out-of-bounds value inside snake_case `generation_config` / `max_output_tokens`.
  nlohmann::json bad_proto_config = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generation_config", {{"temperature", 5.0}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(bad_proto_config),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Missing required `threshold` inside snake_case `safety_settings`.
  nlohmann::json bad_proto_safety = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"safety_settings", nlohmann::json::array({{{"category", "HARM_CATEGORY_HARASSMENT"}}})},
  };
  EXPECT_THAT(payload_schema.validateRequest(bad_proto_safety),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// `fileUri` is required, and `file_uri` names the same field. Both spellings -- and both
// spellings of the parent `fileData` / `file_data` object -- undergo the same presence,
// null, and type checks.
TEST(GeminiGenerateContentTest, FileDataAcceptsEitherSpellingOfFileUri) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_part = [](nlohmann::json part) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({part})}},
                     })},
    };
  };

  EXPECT_THAT(payload_schema.validateRequest(with_part(
                  {{"fileData", {{"mimeType", "video/mp4"}, {"fileUri", "gs://bucket/v.mp4"}}}})),
              IsOk());
  EXPECT_THAT(payload_schema.validateRequest(with_part(
                  {{"fileData", {{"mime_type", "video/mp4"}, {"file_uri", "gs://bucket/v.mp4"}}}})),
              IsOk());
  EXPECT_THAT(
      payload_schema.validateRequest(with_part(
          {{"file_data", {{"mime_type", "video/mp4"}, {"file_uri", "gs://bucket/v.mp4"}}}})),
      IsOk());

  // Neither spelling present is a missing required field under either parent spelling.
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"fileData", {{"mimeType", "video/mp4"}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      payload_schema.validateRequest(with_part({{"file_data", {{"mime_type", "video/mp4"}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // An explicit null leaves the field unset under either spelling.
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"fileData", {{"fileUri", nullptr}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"fileData", {{"file_uri", nullptr}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"file_data", {{"file_uri", nullptr}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Wrong value type is rejected for both `fileUri` and `file_uri`, under both `fileData` and
  // `file_data`.
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"fileData", {{"fileUri", 123}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"fileData", {{"file_uri", 123}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"file_data", {{"file_uri", 123}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// The `Part.data` oneof is enforced by a presence check rather than by the schema walk, so
// it has to see proto-named members too -- otherwise spelling one of them the proto way
// silently disables the check.
TEST(GeminiGenerateContentTest, PartOneOfHonorsProtoFieldNames) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_part = [](nlohmann::json part) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({part})}},
                     })},
    };
  };

  EXPECT_THAT(payload_schema.validateRequest(
                  with_part({{"text", "Hi"}, {"inline_data", {{"data", "AAAA"}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      payload_schema.validateRequest(with_part(
          {{"function_call", {{"name", "f"}}},
           {"function_response", {{"name", "f"}, {"response", nlohmann::json::object()}}}})),
      StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // A null member is unset, so the mixed-spelling pair is still just one member.
  EXPECT_THAT(payload_schema.validateRequest(with_part({{"text", "Hi"}, {"inline_data", nullptr}})),
              IsOk());
}

// Same for the mutually exclusive pairs, including a pair split across the two spellings.
TEST(GeminiGenerateContentTest, MutualExclusionHonorsProtoFieldNames) {
  PayloadSchema payload_schema = createPayloadSchema();

  auto with_generation_config = [](nlohmann::json config) {
    return nlohmann::json{
        {"contents", nlohmann::json::array({
                         {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                     })},
        {"generationConfig", config},
    };
  };

  EXPECT_THAT(payload_schema.validateRequest(
                  with_generation_config({{"response_schema", nlohmann::json::object()},
                                          {"response_json_schema", nlohmann::json::object()}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // Split spelling: one member named the JSON way, the other the proto way.
  EXPECT_THAT(payload_schema.validateRequest(with_generation_config(
                  {{"thinkingConfig", {{"thinking_budget", 1024}, {"thinkingLevel", "HIGH"}}}})),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  nlohmann::json exclusive_declaration = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"tools",
       nlohmann::json::array({
           {{"functionDeclarations",
             nlohmann::json::array({{{"name", "f"},
                                     {"description", "d"},
                                     {"parameters", nlohmann::json::object()},
                                     {"parameters_json_schema", nlohmann::json::object()}}})}},
       })},
  };
  EXPECT_THAT(payload_schema.validateRequest(exclusive_declaration),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

TEST(GeminiGenerateContentTest, InvalidStructuralTypes) {
  PayloadSchema payload_schema = createPayloadSchema();

  // `contents` must be an array, not a bare object.
  nlohmann::json contents_object = {
      {"contents", {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(contents_object),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // `parts` must be an array.
  nlohmann::json parts_object = {
      {"contents", nlohmann::json::array({
                       {{"parts", {{"text", "Hi"}}}},
                   })},
  };
  EXPECT_THAT(payload_schema.validateRequest(parts_object),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // A boolean is neither a number nor a numeric string.
  nlohmann::json boolean_temperature = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig", {{"temperature", true}}},
  };
  EXPECT_THAT(payload_schema.validateRequest(boolean_temperature),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // `generationConfig` must be an object.
  nlohmann::json generation_config_array = {
      {"contents", nlohmann::json::array({
                       {{"parts", nlohmann::json::array({{{"text", "Hi"}}})}},
                   })},
      {"generationConfig", nlohmann::json::array()},
  };
  EXPECT_THAT(payload_schema.validateRequest(generation_config_array),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

// Pins the hand-written list against the schema walk. Any field added, removed or reordered
// in the schema changes the walk order and must be mirrored here.
TEST(GeminiGenerateContentTest, CanonicalStreamableFieldOrder) {
  PayloadSchema payload_schema = createPayloadSchema();
  const std::vector<std::string> expected_order = {
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
  };
  EXPECT_EQ(payload_schema.requestStreamableFieldOrder(), expected_order);
  EXPECT_EQ(payload_schema.requestOffloadableFieldPaths(), expected_order);
}

TEST(GeminiGenerateContentTest, SubSchemasDirectValidation) {
  // partSchema.
  const Schema& part_schema = partSchema();
  nlohmann::json valid_part = {{"text", "Hello"}};
  EXPECT_THAT(part_schema.validate(valid_part), IsOk());

  // contentSchema.
  const Schema& content_schema = contentSchema();
  nlohmann::json valid_content = {{"role", "user"},
                                  {"parts", nlohmann::json::array({{{"text", "Hi"}}})}};
  EXPECT_THAT(content_schema.validate(valid_content), IsOk());

  // safetySettingSchema.
  const Schema& safety_schema = safetySettingSchema();
  nlohmann::json valid_safety = {{"category", "HARM_CATEGORY_HARASSMENT"},
                                 {"threshold", "BLOCK_NONE"}};
  EXPECT_THAT(safety_schema.validate(valid_safety), IsOk());
  nlohmann::json no_category = {{"threshold", "BLOCK_NONE"}};
  EXPECT_THAT(safety_schema.validate(no_category),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // functionDeclarationSchema.
  const Schema& fn_schema = functionDeclarationSchema();
  nlohmann::json valid_fn = {{"name", "f"}, {"description", "d"}};
  EXPECT_THAT(fn_schema.validate(valid_fn), IsOk());

  // toolSchema.
  const Schema& tool_schema = toolSchema();
  nlohmann::json valid_tool = {{"googleSearch", nlohmann::json::object()}};
  EXPECT_THAT(tool_schema.validate(valid_tool), IsOk());

  // toolConfigSchema.
  const Schema& tool_config_schema = toolConfigSchema();
  nlohmann::json valid_tool_config = {{"functionCallingConfig", {{"mode", "AUTO"}}}};
  EXPECT_THAT(tool_config_schema.validate(valid_tool_config), IsOk());

  // generationConfigSchema.
  const Schema& generation_config_schema = generationConfigSchema();
  nlohmann::json valid_generation_config = {{"temperature", 1.0},
                                            {"stopSequences", nlohmann::json::array({"\n"})}};
  EXPECT_THAT(generation_config_schema.validate(valid_generation_config), IsOk());

  // numberOrString accepts both encodings and rejects a boolean.
  Schema numeric = numberOrString();
  nlohmann::json as_number = 1.5;
  nlohmann::json as_string = "1.5";
  nlohmann::json as_bool = true;
  EXPECT_THAT(numeric.validate(as_number), IsOk());
  EXPECT_THAT(numeric.validate(as_string), IsOk());
  EXPECT_THAT(numeric.validate(as_bool), StatusCodeIs(absl::StatusCode::kInvalidArgument));

  // asNullable copies rather than mutating, so the shared sub-schema keeps rejecting null
  // when used as a repeated field's element type.
  nlohmann::json null_value = nullptr;
  EXPECT_THAT(asNullable(contentSchema()).validate(null_value), IsOk());
  EXPECT_THAT(contentSchema().validate(null_value),
              StatusCodeIs(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace Gemini
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
