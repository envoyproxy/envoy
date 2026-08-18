#include "source/extensions/filters/http/mcp_json_rest_bridge/http_request_builder.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule;
using ::Envoy::StatusHelpers::StatusIs;
using ::nlohmann::json;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

TEST(HttpRequestBuilderTest, WildCardHttpRuleBodyContainsAllArgumentsNotInPath) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
get: "/v1/{parent=projects/*}"
body: "*"
)yaml",
                            http_rule);

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
  ASSERT_OK(http_request);

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
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
post: "/v1/{parent=projects/*}"
body: "shelf"
)yaml",
                            http_rule);

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
  ASSERT_OK(http_request);

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
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
put: "/v1/{parent=projects/*}/shelves/{shelf.name}"
)yaml",
                            http_rule);
  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "editions": ["kindle", "hardback", "audobook"]
    },
    "parent": "projects/123456789"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_OK(http_request);

  EXPECT_THAT(
      http_request->url,
      StrEq(
          "/v1/projects/123456789/shelves/"
          "science-fiction?shelf.editions=kindle&shelf.editions=hardback&shelf.editions=audobook"));
  EXPECT_THAT(http_request->method, StrEq("PUT"));
  EXPECT_TRUE(http_request->body.is_null());
}

TEST(HttpRequestBuilderTest, ObjectArrayInQueryParameters) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
patch: "/v1/{parent=projects/*}/shelves/{shelf.name}"
)yaml",
                            http_rule);
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
  ASSERT_OK(http_request);

  EXPECT_THAT(http_request->url,
              StrEq("/v1/projects/123456789/shelves/"
                    "science-fiction?shelf.authors.name=author1&shelf.authors.name=author2"));
  EXPECT_THAT(http_request->method, StrEq("PATCH"));
  EXPECT_TRUE(http_request->body.is_null());
}

TEST(HttpRequestBuilderTest, PrimitiveTypeInQueryParameters) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
delete: "/v1/{parent=projects/*}"
)yaml",
                            http_rule);
  json arguments = json::parse(R"json({
    "integer": 123,
    "float": 123.456,
    "boolean": true,
    "null": null,
    "string": "test string",
    "parent": "projects/123456789"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_OK(http_request);

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789?boolean=true&float=123.456&"
                                       "integer=123&null=null&string=test%20string"));
  EXPECT_THAT(http_request->method, StrEq("DELETE"));
  EXPECT_TRUE(http_request->body.is_null());
}

TEST(HttpRequestBuilderTest, NestedPathInPathTemplate) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
get: "/v1/{parent=projects/*}/shelves/{shelf.name}"
body: "*"
)yaml",
                            http_rule);
  json arguments = json::parse(R"json({
    "shelf": {
      "name": "science-fiction",
      "editions": ["kindle", "hardback", "audobook"]
    },
    "parent": "projects/123456789",
    "theme": "Kids"
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_OK(http_request);

  EXPECT_THAT(http_request->url, StrEq("/v1/projects/123456789/shelves/science-fiction"));
  EXPECT_THAT(http_request->method, StrEq("GET"));
  EXPECT_EQ(http_request->body, json::parse(R"json({
              "shelf": {
                "editions": ["kindle", "hardback", "audobook"]
              },
              "theme": "Kids"
            })json"));
}

// Regression for https://github.com/envoyproxy/envoy/issues/45931: a "simple" path-template
// variable such as {id} must not be able to inject '..' path-traversal segments into the
// constructed upstream path. Before the fix this returned "/v1/users/../../admin/secrets/profile".
TEST(HttpRequestBuilderTest, ConstructBaseUrlSimpleVariableRejectsPathTraversal) {
  json arguments = json::parse(R"json({"id": "../../admin/secrets"})json");
  EXPECT_THAT(constructBaseUrl("/v1/users/{id}/profile", {"id"}, arguments),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// A simple variable's '/' is confined to a single segment (percent-encoded), not treated as a
// path separator that could bypass the intended route prefix.
TEST(HttpRequestBuilderTest, ConstructBaseUrlSimpleVariableEncodesSlash) {
  json arguments = json::parse(R"json({"id": "a/b"})json");
  absl::StatusOr<std::string> url = constructBaseUrl("/v1/users/{id}/profile", {"id"}, arguments);
  ASSERT_TRUE(url.ok());
  EXPECT_THAT(*url, StrEq("/v1/users/a%2Fb/profile"));
}

// A benign single-segment value still substitutes unchanged.
TEST(HttpRequestBuilderTest, ConstructBaseUrlSimpleVariableAllowsSingleSegment) {
  json arguments = json::parse(R"json({"id": "alice"})json");
  absl::StatusOr<std::string> url = constructBaseUrl("/v1/users/{id}/profile", {"id"}, arguments);
  ASSERT_TRUE(url.ok());
  EXPECT_THAT(*url, StrEq("/v1/users/alice/profile"));
}

// A wildcard variable ({var=pattern}) may legitimately span multiple segments, so its '/' is
// preserved and left as the operator's explicit choice.
TEST(HttpRequestBuilderTest, ConstructBaseUrlWildcardVariablePreservesSlash) {
  json arguments = json::parse(R"json({"parent": "projects/123456789"})json");
  absl::StatusOr<std::string> url =
      constructBaseUrl("/v1/{parent=projects/*}/shelves", {"parent"}, arguments);
  ASSERT_TRUE(url.ok());
  EXPECT_THAT(*url, StrEq("/v1/projects/123456789/shelves"));
}

// #45931 defense-in-depth: a '..' traversal segment is rejected even for a wildcard variable, whose
// value is equally attacker-controlled. Before the broadened check this produced
// "/v1/../../admin/secrets/shelves".
TEST(HttpRequestBuilderTest, ConstructBaseUrlWildcardVariableRejectsPathTraversal) {
  json arguments = json::parse(R"json({"name": "../../admin/secrets"})json");
  EXPECT_THAT(constructBaseUrl("/v1/{name=projects/*}/shelves", {"name"}, arguments),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// '\' is percent-encoded to %5C rather than reaching the upstream literally, but an upstream that
// decodes it and folds it to '/' would still see a traversal, so '\' is treated as a segment
// separator by the check as well.
TEST(HttpRequestBuilderTest, ConstructBaseUrlRejectsBackslashPathTraversal) {
  json arguments = json::parse(R"json({"id": "..\\.."})json");
  EXPECT_THAT(constructBaseUrl("/v1/users/{id}/profile", {"id"}, arguments),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// A backslash that is not a traversal segment is still allowed through, percent-encoded.
TEST(HttpRequestBuilderTest, ConstructBaseUrlEncodesNonTraversalBackslash) {
  json arguments = json::parse(R"json({"id": "a\\b"})json");
  absl::StatusOr<std::string> url = constructBaseUrl("/v1/users/{id}/profile", {"id"}, arguments);
  ASSERT_TRUE(url.ok());
  EXPECT_THAT(*url, StrEq("/v1/users/a%5Cb/profile"));
}

TEST(HttpRequestBuilderTest, PathTemplateNotInArgumentsReturnError) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
get: "/v1/{parent=projects/*}"
)yaml",
                            http_rule);
  json arguments = json::parse(R"json({
    "string": "test string"
  })json");

  EXPECT_THAT(buildHttpRequest(http_rule, arguments), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpRequestBuilderTest, FailToExtractBodyReturnError) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
get: "/v1"
body: "foo"
)yaml",
                            http_rule);
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

// Substitution matches a whole `{name}` or `{name=pattern}` and nothing else. These cases pin the
// boundaries that the previous `\{name(?:=[^}]+)?\}` regex enforced.
TEST(HttpRequestBuilderTest, ConstructBaseUrlVariableMatchingIsExact) {
  json arguments = json::parse(R"json({"id": "7"})json");

  // A longer name that merely starts with the variable name is left alone.
  EXPECT_THAT(*constructBaseUrl("/v1/{identifier}/{id}", {"id"}, arguments),
              StrEq("/v1/{identifier}/7"));

  // An explicit pattern needs at least one character, so `{id=}` is not a variable.
  EXPECT_THAT(*constructBaseUrl("/v1/{id=}", {"id"}, arguments), StrEq("/v1/{id=}"));

  // Every occurrence is substituted, not just the first.
  EXPECT_THAT(*constructBaseUrl("/v1/{id}/copy/{id}", {"id"}, arguments), StrEq("/v1/7/copy/7"));
}

TEST(HttpRequestBuilderTest, FailToExtractValueFromParameterBindingReturnOk) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1"
    bindings:
    - type: HEADER
      name: "foo"
      argument_path: "foo_key"
    - type: COOKIE
      name: "bar"
      argument_path: "bar_key"
  )yaml",
                            http_rule);
  json arguments = json::parse(R"json({})json");

  EXPECT_OK(buildHttpRequest(http_rule, arguments));
}

TEST(HttpRequestBuilderTest, WildCardBodyAndParameterBindingPathNotFoundInEmptyObject) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1/{parent=projects/*}"
    body: "*"
    bindings:
    - type: HEADER
      name: "X-Header-Key"
      argument_path: "nested.header_key"
  )yaml",
                            http_rule);
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
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1/{parent=projects/*}/apiKeys"
    bindings:
    - type: HEADER
      name: "X-Api-Key"
      argument_path: "api_key"
    - type: HEADER
      name: "Authorization"
      argument_path: "auth_token"
    - type: COOKIE
      name: "SESSION_ID"
      argument_path: "session_id"
    - type: COOKIE
      name: "PREF"
      argument_path: "pref"
  )yaml",
                            http_rule);

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
      UnorderedElementsAre(Pair("X-Api-Key", "my-key"), Pair("Authorization", "Bearer xyz")));

  // Verify cookie params are populated correctly.
  EXPECT_THAT(http_request->cookies_params,
              UnorderedElementsAre(Pair("SESSION_ID", "sess-123"), Pair("PREF", "dark-mode")));
}

TEST(HttpRequestBuilderTest, WildCardBodyExcludesHeaderAndCookieBindings) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    post: "/v1/{parent=projects/*}"
    body: "*"
    bindings:
    - type: HEADER
      name: "X-Api-Key"
      argument_path: "api_key"
    - type: COOKIE
      name: "SESSION_ID"
      argument_path: "session_id"
  )yaml",
                            http_rule);

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
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1/resources"
    bindings:
    - type: HEADER
      name: "X-Auth-Token"
      argument_path: "user.auth_token"
    - type: COOKIE
      name: "SESSION"
      argument_path: "context.session_id"
  )yaml",
                            http_rule);

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
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    put: "/v1/{parent=projects/*}"
    body: "payload"
    bindings:
    - type: HEADER
      name: "X-Request-Id"
      argument_path: "request_id"
    - type: COOKIE
      name: "TOKEN"
      argument_path: "token"
  )yaml",
                            http_rule);

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

TEST(HttpRequestBuilderTest, InvalidHeaderValueRejection) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1"
    bindings:
    - type: HEADER
      name: "X-Header-Key"
      argument_path: "header_key"
  )yaml",
                            http_rule);

  // CR/LF is invalid character in header values.
  json arguments = json::parse(R"json({
    "header_key": "invalid\r\nvalue"
  })json");

  EXPECT_THAT(buildHttpRequest(http_rule, arguments), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpRequestBuilderTest, InvalidCookieValueRejection) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1"
    bindings:
    - type: COOKIE
      name: "SESSION"
      argument_path: "session_id"
  )yaml",
                            http_rule);

  // CR/LF is invalid character.
  EXPECT_THAT(
      buildHttpRequest(http_rule, json::parse(R"json({"session_id": "invalid\r\nvalue"})json")),
      StatusIs(absl::StatusCode::kInvalidArgument));

  // Space is invalid.
  EXPECT_THAT(
      buildHttpRequest(http_rule, json::parse(R"json({"session_id": "invalid value"})json")),
      StatusIs(absl::StatusCode::kInvalidArgument));

  // Comma is invalid.
  EXPECT_THAT(
      buildHttpRequest(http_rule, json::parse(R"json({"session_id": "invalid,value"})json")),
      StatusIs(absl::StatusCode::kInvalidArgument));

  // Semicolon is invalid.
  EXPECT_THAT(
      buildHttpRequest(http_rule, json::parse(R"json({"session_id": "invalid;value"})json")),
      StatusIs(absl::StatusCode::kInvalidArgument));

  // Backslash is invalid.
  EXPECT_THAT(
      buildHttpRequest(http_rule, json::parse(R"json({"session_id": "invalid\\value"})json")),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpRequestBuilderTest, ValidQuotedCookieValueAllowed) {
  HttpRule http_rule;
  TestUtility::loadFromYaml(R"yaml(
    get: "/v1"
    bindings:
    - type: COOKIE
      name: "SESSION"
      argument_path: "session_id"
  )yaml",
                            http_rule);

  // Enclosed in DQUOTE is valid.
  json arguments = json::parse(R"json({
    "session_id": "\"valid-session-id\""
  })json");

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(http_rule, arguments);
  ASSERT_TRUE(http_request.ok());
  EXPECT_THAT(http_request->cookies_params,
              UnorderedElementsAre(Pair("SESSION", "\"valid-session-id\"")));
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
