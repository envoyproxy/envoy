#include "source/extensions/filters/http/mcp_json_rest_bridge/http_request_builder.h"

#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "ocpdiag/core/testing/parse_text_proto.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule;
using ::Envoy::StatusHelpers::StatusIs;
using ::nlohmann::json;
using ::ocpdiag::testing::ParseTextProtoOrDie;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

TEST(HttpRequestBuilderTest, WildCardHttpRuleBodyContainsAllArgumentsNotInPath) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1/{parent=projects/*}"
    body: "*"
  )pb");

  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "code": 3,
      "content": "Some random content",
      "active": true,
      "editions": ["kindle", "hardback", "audobook"],
      "authors": [
        {"name": "author1"},
        {"name": "author2"}
      ]
    },
    "parent": "projects/123456789",
    "theme": "Kids"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789"));
  EXPECT_THAT(http_request->method, StrEq("GET"));
  EXPECT_EQ(http_request->body, json::parse(R"json({
              "shelf": {
                "name": "science-fiction",
                "code": 3,
                "content": "Some random content",
                "active": true,
                "editions": ["kindle", "hardback", "audobook"],
                "authors": [
                  {"name": "author1"},
                  {"name": "author2"}
                ]
              },
              "theme": "Kids"
            })json"));
}

TEST(HttpRequestBuilderTest, ExtractHttpRuleBody) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    post: "/v1/{parent=projects/*}"
    body: "shelf"
  )pb");

  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "code": 3,
      "content": "Some random content",
      "active": true,
      "editions": ["kindle", "hardback", "audobook"],
      "authors": [
        {"name": "author1"},
        {"name": "author2"}
      ]
    },
    "parent": "projects/123456789",
    "theme": "Kids"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789?theme=Kids"));
  EXPECT_THAT(http_request->method, StrEq("POST"));
  EXPECT_EQ(http_request->body, json::parse(R"json({
              "name": "science-fiction",
              "code": 3,
              "content": "Some random content",
              "active": true,
              "editions": ["kindle", "hardback", "audobook"],
              "authors": [
                {"name": "author1"},
                {"name": "author2"}
              ]
            })json"));
}

TEST(HttpRequestBuilderTest, PrimitiveArrayInQueryParameters) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    put: "/v1/{parent=projects/*}/shelves/{shelf.name}"
  )pb");
  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "editions": ["kindle", "hardback", "audobook"]
    },
    "parent": "projects/123456789"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(
      http_request->url,
      StrEq(
          "/v1/projects/123456789/shelves/"
          "science-fiction?shelf.editions=kindle&shelf.editions=hardback&shelf.editions=audobook"));
  EXPECT_THAT(http_request->method, StrEq("PUT"));
  EXPECT_TRUE(http_request->body.is_null());
}

TEST(HttpRequestBuilderTest, ObjectArrayInQueryParameters) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    patch: "/v1/{parent=projects/*}/shelves/{shelf.name}"
  )pb");
  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "authors": [
        {"name": "author1"},
        {"name": "author2"}
      ]
    },
    "parent": "projects/123456789"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url,
              StrEq("/v1/projects/123456789/shelves/"
                    "science-fiction?shelf.authors.name=author1&shelf.authors.name=author2"));
  EXPECT_THAT(http_request->method, StrEq("PATCH"));
  EXPECT_TRUE(http_request->body.is_null());
}

TEST(HttpRequestBuilderTest, PrimitiveTypeInQueryParameters) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    delete: "/v1/{parent=projects/*}"
  )pb");
  json arguments = json::parse(R"json({
    "integer": 123,
    "float": 123.456,
    "boolean": true,
    "null": null,
    "string": "test string",
    "parent": "projects/123456789"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789?boolean=true&float=123.456&"
                                       "integer=123&null=null&string=test%20string"));
  EXPECT_THAT(http_request->method, StrEq("DELETE"));
  EXPECT_TRUE(http_request->body.is_null());
}

TEST(HttpRequestBuilderTest, NestedPathInPathTemplate) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1/{parent=projects/*}/shelves/{shelf.name}"
    body: "*"
  )pb");
  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "editions": ["kindle", "hardback", "audobook"]
    },
    "parent": "projects/123456789",
    "theme": "Kids"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789/shelves/science-fiction"));
  EXPECT_THAT(http_request->method, StrEq("GET"));
  EXPECT_EQ(http_request->body, json::parse(R"json({
              "shelf": {
                "editions": ["kindle", "hardback", "audobook"]
              },
              "theme": "Kids"
            })json"));
}

TEST(HttpRequestBuilderTest, PathTemplateNotInArgumentsReturnError) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1/{parent=projects/*}"
  )pb");
  json arguments = json::parse(R"json({
    "string": "test string"
  })json");

  EXPECT_THAT(buildHttpRequest(http_rule, arguments), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpRequestBuilderTest, FailToExtractBodyReturnError) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1"
    body: "foo"
  )pb");
  json arguments = json::parse(R"json({})json");

  EXPECT_THAT(buildHttpRequest(http_rule, arguments), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpRequestBuilderTest, ConstructBaseUrlTest) {
  json arguments = json::parse(R"json({
    "parent": "projects/123456789",
    "tableId": "table_A",
    "datasetId": "dataset_B",
    "projectId": "project_C"
  })json");

  // Single substitution.
  EXPECT_THAT(*constructBaseUrl("/v1/{parent=projects/*}", {"parent"}, arguments),
              StrEq("/v1/projects/123456789"));

  // Multiple substitutions.
  EXPECT_THAT(*constructBaseUrl(
                  "/test/v2/projects/{projectId}/datasets/{datasetId}/tables/{tableId}/insertAll",
                  {"projectId", "datasetId", "tableId"}, arguments),
              StrEq("/test/v2/projects/project_C/datasets/dataset_B/tables/table_A/insertAll"));

  // Missing argument.
  EXPECT_THAT(constructBaseUrl("/v1/{missing}", {"missing"}, arguments),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpRequestBuilderTest, FailToExtractValueFromParameterBindingReturnOk) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1"
    header_parameter_bindings { name: "foo" argument_path: "foo_key" }
    cookie_parameter_bindings { name: "bar" argument_path: "bar_key" }
  )pb");
  json arguments = json::parse(R"json({})json");

  EXPECT_OK(buildHttpRequest(http_rule, arguments));
}

TEST(HttpRequestBuilderTest, WildCardBodyAndParameterBindingPathNotFoundInEmptyObject) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1/{parent=projects/*}"
    body: "*"
    header_parameter_bindings { name: "X-Header-Key" argument_path: "nested.header_key" }
  )pb");
  json arguments = json::parse(R"json({
    "parent": "projects/123456789",
    "nested": {},
    "theme": "Kids"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789"));
  EXPECT_THAT(http_request->method, StrEq("GET"));
  EXPECT_EQ(http_request->body, json::parse(R"json({
              "nested": {},
              "theme": "Kids"
            })json"));
  EXPECT_THAT(http_request->headers_params, IsEmpty());
  EXPECT_THAT(http_request->cookies_params, IsEmpty());
}

TEST(HttpRequestBuilderTest, HeaderAndCookieParamsPopulatedCorrectly) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1/{parent=projects/*}/apiKeys"
    header_parameter_bindings { name: "X-Api-Key" argument_path: "api_key" }
    header_parameter_bindings { name: "Authorization" argument_path: "auth_token" }
    cookie_parameter_bindings { name: "SESSION_ID" argument_path: "session_id" }
    cookie_parameter_bindings { name: "PREF" argument_path: "pref" }
  )pb");

  json arguments = json::parse(R"json({
    "parent": "projects/123456789",
    "api_key": "my-key",
    "auth_token": "Bearer xyz",
    "session_id": "sess-123",
    "pref": "dark-mode",
    "page_size": 10
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789/apiKeys?page_size=10"));
  EXPECT_THAT(http_request->method, StrEq("GET"));
  EXPECT_TRUE(http_request->body.is_null());

  // Verify header params are populated correctly.
  EXPECT_THAT(
      http_request->headers_params,
      UnorderedElementsAre(Pair("X-Api-Key", "my-key"), Pair("Authorization", "Bearer%20xyz")));

  // Verify cookie params are populated correctly.
  EXPECT_THAT(http_request->cookies_params,
              UnorderedElementsAre(Pair("SESSION_ID", "sess-123"), Pair("PREF", "dark-mode")));
}

TEST(HttpRequestBuilderTest, WildCardBodyExcludesHeaderAndCookieBindings) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    post: "/v1/{parent=projects/*}"
    body: "*"
    header_parameter_bindings { name: "X-Api-Key" argument_path: "api_key" }
    cookie_parameter_bindings { name: "SESSION_ID" argument_path: "session_id" }
  )pb");

  json arguments = json::parse(R"json({
    "parent": "projects/123456789",
    "api_key": "my-key",
    "session_id": "sess-123",
    "payload": "data"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789"));
  EXPECT_THAT(http_request->method, StrEq("POST"));

  // Body should NOT contain api_key, session_id, or parent (path template).
  EXPECT_EQ(http_request->body, json::parse(R"json({"payload": "data"})json"));

  // But headers and cookies should be populated.
  EXPECT_THAT(http_request->headers_params, UnorderedElementsAre(Pair("X-Api-Key", "my-key")));
  EXPECT_THAT(http_request->cookies_params, UnorderedElementsAre(Pair("SESSION_ID", "sess-123")));
}

TEST(HttpRequestBuilderTest, NestedArgumentPathForBindings) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    get: "/v1/resources"
    header_parameter_bindings { name: "X-Auth-Token" argument_path: "user.auth_token" }
    cookie_parameter_bindings { name: "SESSION" argument_path: "context.session_id" }
  )pb");

  json arguments = json::parse(R"json({
    "user": {
      "auth_token": "token-abc",
      "name": "alice"
    },
    "context": {
      "session_id": "sess-xyz",
      "locale": "en"
    }
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  // Bound nested paths should NOT appear in query params.
  // user.name and context.locale should appear as query params.
  EXPECT_THAT(http_request->url, StrEq("/v1/resources?context.locale=en&user.name=alice"));
  EXPECT_THAT(http_request->method, StrEq("GET"));
  EXPECT_TRUE(http_request->body.is_null());

  EXPECT_THAT(http_request->headers_params,
              UnorderedElementsAre(Pair("X-Auth-Token", "token-abc")));
  EXPECT_THAT(http_request->cookies_params, UnorderedElementsAre(Pair("SESSION", "sess-xyz")));
}

TEST(HttpRequestBuilderTest, SpecificBodyFieldWithHeaderCookieBindings) {
  HttpRule http_rule = ParseTextProtoOrDie(
      R"pb(
    put: "/v1/{parent=projects/*}"
    body: "payload"
    header_parameter_bindings { name: "X-Request-Id" argument_path: "request_id" }
    cookie_parameter_bindings { name: "TOKEN" argument_path: "token" }
  )pb");

  json arguments = json::parse(R"json({
    "parent": "projects/123",
    "payload": {"data": "value"},
    "request_id": "req-001",
    "token": "tok-abc",
    "extra_query": "query_val"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());

  // extra_query should be a query param; request_id and token should NOT.
  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123?extra_query=query_val"));
  EXPECT_THAT(http_request->method, StrEq("PUT"));
  EXPECT_EQ(http_request->body, json::parse(R"json({"data": "value"})json"));

  EXPECT_THAT(http_request->headers_params, UnorderedElementsAre(Pair("X-Request-Id", "req-001")));
  EXPECT_THAT(http_request->cookies_params, UnorderedElementsAre(Pair("TOKEN", "tok-abc")));
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
