#include <string>
#include <vector>

#include "envoy/type/ai/v3/endpoint.pb.h"
#include "envoy/type/ai/v3/endpoint.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/endpoint_layout.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using envoy::type::ai::v3::Endpoint;
using envoy::type::ai::v3::EndpointTemplate;
using ProtoLLMProtocol = envoy::type::ai::v3::LLMProtocol;
using testing::HasSubstr;

constexpr ProtoLLMProtocol ChatCompletions = envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS;
constexpr ProtoLLMProtocol Responses = envoy::type::ai::v3::OPENAI_RESPONSES;
constexpr ProtoLLMProtocol Messages = envoy::type::ai::v3::ANTHROPIC_MESSAGES;
constexpr ProtoLLMProtocol Gemini = envoy::type::ai::v3::GEMINI_GENERATE_CONTENT;

Endpoint endpointFromYaml(const std::string& yaml) {
  Endpoint endpoint;
  TestUtility::loadFromYaml(yaml, endpoint);
  return endpoint;
}

absl::Status validate(const std::string& yaml, ProtoLLMProtocol protocol = ChatCompletions) {
  return validateEndpoint(endpointFromYaml(yaml), protocol);
}

MATCHER_P(IsInvalid, substring, "") {
  return arg.code() == absl::StatusCode::kInvalidArgument &&
         testing::ExplainMatchResult(HasSubstr(substring), std::string(arg.message()),
                                     result_listener);
}

// Every preset's layouts are well-formed templates, fed the preset's required variables.
TEST(EndpointPresetTest, LayoutsAreValidTemplates) {
  ASSERT_EQ(endpointPresets().size(), 7);
  for (const EndpointPreset& preset : endpointPresets()) {
    ASSERT_FALSE(preset.layouts.empty()) << preset.name;
    for (const auto& [protocol, layout] : preset.layouts) {
      Endpoint endpoint;
      *endpoint.mutable_custom() = layout;
      for (const std::string& name : preset.required_variables) {
        (*endpoint.mutable_variables())[name] = "value";
      }
      TestUtility::validate(endpoint);
      EXPECT_OK(validateEndpoint(endpoint, protocol)) << preset.name << " " << protocol;

      Endpoint by_name;
      by_name.set_preset(preset.name);
      *by_name.mutable_variables() = endpoint.variables();
      EXPECT_OK(validateEndpoint(by_name, protocol)) << preset.name << " " << protocol;
    }
  }
}

TEST(EndpointPresetTest, Table) {
  const EndpointPreset* openai = findEndpointPreset("openai");
  ASSERT_NE(openai, nullptr);
  EXPECT_EQ(openai->layout(Responses)->path_template(), "/v1/responses");
  EXPECT_EQ(openai->layout(Messages), nullptr);

  const EndpointPreset* vertex = findEndpointPreset("gcp_vertex_ai");
  ASSERT_NE(vertex, nullptr);
  const EndpointTemplate* claude = vertex->layout(Messages);
  ASSERT_NE(claude, nullptr);
  EXPECT_EQ(claude->path_template(),
            "/v1/projects/{project}/locations/{location}/publishers/anthropic/models/"
            "{model}:{method}");
  EXPECT_EQ(claude->stream_placement(), EndpointTemplate::STREAM_IN_BODY_AND_METHOD);
  EXPECT_EQ(claude->set_body_fields().fields().at("anthropic_version").string_value(),
            "vertex-2023-10-16");
  EXPECT_EQ(vertex->required_variables, (std::vector<std::string>{"project", "location"}));

  const EndpointPreset* bedrock = findEndpointPreset("aws_bedrock");
  ASSERT_NE(bedrock, nullptr);
  EXPECT_EQ(bedrock->layout(Messages)->response_framing(), EndpointTemplate::AWS_EVENT_STREAM);
  EXPECT_EQ(bedrock->optional_variables, (std::vector<std::string>{"region"}));

  EXPECT_EQ(findEndpointPreset("vertex"), nullptr);
}

TEST(EndpointPresetTest, Validation) {
  EXPECT_OK(validate("preset: openai", Responses));
  EXPECT_THAT(validate("preset: nope"), IsInvalid("unknown endpoint preset 'nope'"));
  EXPECT_THAT(validate("preset: gcp_vertex_ai_express", Messages),
              IsInvalid("does not serve ANTHROPIC_MESSAGES"));
  EXPECT_THAT(validate("{preset: gcp_vertex_ai, variables: {project: p}}", Gemini),
              IsInvalid("requires variable 'location'"));
  EXPECT_THAT(validate("{preset: openai, variables: {region: us}}"),
              IsInvalid("takes no variable 'region'"));
  EXPECT_OK(validate("{preset: aws_bedrock, variables: {region: us-east-1}}", Messages));
  EXPECT_OK(validate("preset: aws_bedrock", Messages));
  EXPECT_OK(validate("{preset: azure_openai, variables: {api_version: 2024-10-21}}"));
}

TEST(EndpointVariableTest, Values) {
  const auto with_project = [](absl::string_view value) {
    Endpoint endpoint = endpointFromYaml("{preset: gcp_vertex_ai, variables: {location: l}}");
    (*endpoint.mutable_variables())["project"] = std::string(value);
    return validateEndpoint(endpoint, Gemini);
  };
  EXPECT_OK(with_project("my-project_1.a"));
  EXPECT_OK(with_project(std::string(256, 'p')));
  EXPECT_THAT(with_project(std::string(257, 'p')), IsInvalid("1 to 256 bytes"));
  EXPECT_THAT(with_project(""), IsInvalid("variable 'project' must be 1 to 256 bytes"));
  EXPECT_THAT(with_project("a..b"), IsInvalid("must not contain '..'"));
  EXPECT_THAT(with_project("."), IsInvalid("variable 'project' must not be '.'"));
  for (const absl::string_view invalid :
       {absl::string_view("a/b"), absl::string_view("a?b"), absl::string_view("a#b"),
        absl::string_view("a%2Fb"), absl::string_view("a b"), absl::string_view("a\tb"),
        absl::string_view("a\0b", 3), absl::string_view("a\x7f"),
        absl::string_view("caf\xc3\xa9")}) {
    EXPECT_THAT(with_project(invalid), IsInvalid("must not contain")) << invalid;
  }
}

TEST(EndpointLayoutTest, NoLayout) {
  EXPECT_THAT(validateEndpoint(Endpoint(), ChatCompletions), IsInvalid("a preset or a template"));
}

constexpr absl::string_view FullTemplate = R"EOF(
custom:
  path_template: /v1/{tenant}/models/{model}:{method}
  unary_method: generate
  stream_method: streamGenerate
  query_params: {api-version: "{version}", fixed: "v-{version}-x"}
  stream_query_params: {alt: sse}
  model_placement: MODEL_IN_PATH
  stream_placement: STREAM_IN_BODY_AND_METHOD
  response_framing: NDJSON
  set_body_fields: {envelope: 1}
  remove_body_fields: [model]
  request_headers_to_add: {x-tenant: acme}
  request_headers_to_remove: [authorization, x-api-key, anthropic-version]
variables: {tenant: acme, version: "2025-01-01"}
)EOF";

TEST(EndpointTemplateTest, FullTemplateIsValid) {
  EXPECT_OK(validate(std::string(FullTemplate)));
  // A template serves whichever protocol it is paired with.
  EXPECT_OK(validate(std::string(FullTemplate), Gemini));
}

TEST(EndpointTemplateTest, PathTemplate) {
  EXPECT_THAT(validate("custom: {path_template: ''}"), IsInvalid("must start with '/'"));
  EXPECT_THAT(validate("custom: {path_template: v1/chat}"), IsInvalid("must start with '/'"));
  EXPECT_THAT(validate("custom: {path_template: '/a}'}"), IsInvalid("'}' with no '{'"));
  EXPECT_THAT(validate("custom: {path_template: '/a/{b'}"), IsInvalid("'{' with no '}'"));
  for (const absl::string_view malformed : {"/{}", "/{Model}", "/{1a}", "/{a-b}", "/{a{b}"}) {
    EXPECT_THAT(validate(absl::StrCat("custom: {path_template: '", malformed, "'}")),
                IsInvalid("malformed placeholder"))
        << malformed;
  }
  for (const absl::string_view literal : {"/a?b", "/a#b", "/a b", "/a\\tb", "/caf\xc3\xa9"}) {
    EXPECT_THAT(validate(absl::StrCat("custom: {path_template: \"", literal, "\"}")),
                IsInvalid("must not contain '?'"))
        << literal;
  }
  EXPECT_THAT(validate("custom: {path_template: '/{project}'}"),
              IsInvalid("path_template names no variable 'project'"));
}

TEST(EndpointTemplateTest, ModelPlacement) {
  EXPECT_OK(validate("custom: {path_template: '/m/{model}', model_placement: MODEL_IN_PATH}"));
  EXPECT_THAT(validate("custom: {path_template: '/m/{model}'}"), IsInvalid("{model}"));
  EXPECT_THAT(validate("custom: {path_template: '/m', model_placement: MODEL_IN_PATH}"),
              IsInvalid("{model}"));
}

TEST(EndpointTemplateTest, StreamPlacement) {
  const std::string methods = "unary_method: u, stream_method: s";
  EXPECT_OK(validate(absl::StrCat("custom: {path_template: '/{method}', ", methods,
                                  ", stream_placement: STREAM_IN_METHOD}")));
  // {method} without a method placement.
  EXPECT_THAT(validate(absl::StrCat("custom: {path_template: '/{method}', ", methods, "}")),
              IsInvalid("{method}"));
  // A method placement without {method}.
  EXPECT_THAT(validate(absl::StrCat("custom: {path_template: '/m', ", methods,
                                    ", stream_placement: STREAM_IN_BODY_AND_METHOD}")),
              IsInvalid("{method}"));
  // A method placement without both methods.
  for (const absl::string_view only : {"unary_method: u", "stream_method: s"}) {
    EXPECT_THAT(validate(absl::StrCat("custom: {path_template: '/{method}', ", only,
                                      ", stream_placement: STREAM_IN_METHOD}")),
                IsInvalid("requires unary_method and stream_method"))
        << only;
  }
  // Methods are path content.
  EXPECT_THAT(validate("custom: {path_template: '/{method}', unary_method: 'a/b', "
                       "stream_method: s, stream_placement: STREAM_IN_METHOD}"),
              IsInvalid("unary_method must not contain"));
  EXPECT_THAT(validate("custom: {path_template: '/{method}', unary_method: u, "
                       "stream_method: 's?x', stream_placement: STREAM_IN_METHOD}"),
              IsInvalid("stream_method must not contain"));
}

TEST(EndpointTemplateTest, Variables) {
  EXPECT_OK(validate("{custom: {path_template: '/{a}/{b}'}, variables: {a: x, b: z}}"));
  EXPECT_THAT(validate("{custom: {path_template: '/{a}'}, variables: {a: x, b: z}}"),
              IsInvalid("variable 'b' is not used by the template"));
  // model and method are not variables.
  EXPECT_THAT(validate("{custom: {path_template: '/m'}, variables: {model: x}}"),
              IsInvalid("variable 'model' is not used"));
  EXPECT_THAT(validate("{custom: {path_template: '/{a}'}, variables: {a: 'x/y'}}"),
              IsInvalid("variable 'a' must not contain"));
}

TEST(EndpointTemplateTest, QueryParams) {
  EXPECT_OK(validate("{custom: {path_template: /m, query_params: {v: 'x{a}'}}, "
                     "variables: {a: '1'}}"));
  EXPECT_THAT(validate("custom: {path_template: /m, query_params: {'': x}}"),
              IsInvalid("query parameter name must be 1 to 256 bytes"));
  EXPECT_THAT(validate("custom: {path_template: /m, stream_query_params: {'a b': x}}"),
              IsInvalid("query parameter name must not contain"));
  EXPECT_THAT(validate("custom: {path_template: /m, query_params: {v: ''}}"),
              IsInvalid("query parameter 'v' must be 1 to 256 bytes"));
  EXPECT_THAT(validate("custom: {path_template: /m, stream_query_params: {v: 'a#b'}}"),
              IsInvalid("query parameter 'v' must not contain"));
  EXPECT_THAT(validate("custom: {path_template: /m, query_params: {v: '{a}'}}"),
              IsInvalid("query parameter 'v' names no variable 'a'"));
  EXPECT_THAT(validate("custom: {path_template: /m, query_params: {v: '.'}}"),
              IsInvalid("query parameter 'v' must not be '.'"));
  EXPECT_THAT(validate("custom: {path_template: /m, query_params: {v: '{model}'}}"),
              IsInvalid("reserved placeholder '{model}'"));
  EXPECT_THAT(validate("{custom: {path_template: '/m/{model}', model_placement: MODEL_IN_PATH, "
                       "query_params: {m: '{model}'}}, variables: {model: x}}"),
              IsInvalid("reserved placeholder '{model}'"));
  EXPECT_THAT(validate("{custom: {path_template: '/{method}', unary_method: u, stream_method: s, "
                       "stream_placement: STREAM_IN_METHOD, stream_query_params: {m: '{method}'}}, "
                       "variables: {method: x}}"),
              IsInvalid("reserved placeholder '{method}'"));
  EXPECT_THAT(validate("custom: {path_template: /m, query_params: {v: '{A}'}}"),
              IsInvalid("query parameter 'v' has a malformed placeholder"));
  // Substitution can make a value too long, or join two halves of '..'.
  EXPECT_THAT(validate(absl::StrCat("{custom: {path_template: /m, query_params: {v: 'x{a}'}}, "
                                    "variables: {a: '",
                                    std::string(256, 'a'), "'}}")),
              IsInvalid("1 to 256 bytes"));
  EXPECT_THAT(validate("{custom: {path_template: /m, query_params: {v: '.{a}'}}, "
                       "variables: {a: '.x'}}"),
              IsInvalid("must not contain '..'"));
}

TEST(EndpointTemplateTest, Headers) {
  const auto with_header = [](absl::string_view name, absl::string_view value) {
    Endpoint endpoint = endpointFromYaml("custom: {path_template: /m}");
    (*endpoint.mutable_custom()->mutable_request_headers_to_add())[std::string(name)] =
        std::string(value);
    return validateEndpoint(endpoint, ChatCompletions);
  };
  EXPECT_OK(with_header("anthropic-version", "2023-06-01"));
  EXPECT_THAT(with_header("Anthropic-Version", "x"), IsInvalid("valid lowercase name"));
  EXPECT_THAT(with_header("", "x"), IsInvalid("valid lowercase name"));
  EXPECT_THAT(with_header("a b", "x"), IsInvalid("valid lowercase name"));
  EXPECT_THAT(with_header(":path", "/x"), IsInvalid("pseudo-header or host"));
  EXPECT_THAT(with_header("host", "x"), IsInvalid("pseudo-header or host"));
  for (const absl::string_view credential : {"authorization", "proxy-authorization", "x-api-key",
                                             "api-key", "x-goog-api-key", "cookie"}) {
    EXPECT_THAT(with_header(credential, "secret"), IsInvalid("carries a credential")) << credential;
  }
  for (const absl::string_view value :
       {absl::string_view("a\rb"), absl::string_view("a\nb"), absl::string_view("a\0b", 3)}) {
    EXPECT_THAT(with_header("x-a", value), IsInvalid("invalid value"));
  }

  const auto removing = [](absl::string_view name) {
    Endpoint endpoint = endpointFromYaml("custom: {path_template: /m}");
    endpoint.mutable_custom()->add_request_headers_to_remove(std::string(name));
    return validateEndpoint(endpoint, ChatCompletions);
  };
  EXPECT_OK(removing("authorization"));
  EXPECT_OK(removing("cookie"));
  EXPECT_THAT(removing("X-Api-Key"), IsInvalid("valid lowercase name"));
  EXPECT_THAT(removing(":authority"), IsInvalid("pseudo-header or host"));
  EXPECT_THAT(removing("host"), IsInvalid("pseudo-header or host"));
}

TEST(EndpointTemplateTest, BodyFields) {
  EXPECT_OK(validate("custom: {path_template: /m, set_body_fields: {a: {b: 1}}, "
                     "remove_body_fields: [c]}"));
  EXPECT_THAT(validate("custom: {path_template: /m, set_body_fields: {'': 1}}"),
              IsInvalid("set_body_fields must not have an empty field name"));
  Endpoint endpoint = endpointFromYaml("custom: {path_template: /m}");
  endpoint.mutable_custom()->add_remove_body_fields("");
  EXPECT_THAT(validateEndpoint(endpoint, ChatCompletions),
              IsInvalid("remove_body_fields must not have an empty field name"));
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
