#include "source/extensions/filters/http/mcp_json_rest_bridge/http_request_builder.h"

#include "test/test_common/status_utility.h"

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
using ::testing::StrEq;

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

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
