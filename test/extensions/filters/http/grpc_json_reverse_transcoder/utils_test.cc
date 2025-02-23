#include <string>

#include "source/extensions/filters/http/grpc_json_reverse_transcoder/utils.h"

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

namespace {
TEST(BuildQueryParamString, ReturnsQueryParamString) {
  nlohmann::json object = R"({
"shelf": {
  "name": "science-fiction",
  "code": 3,
  "content": "Some random content",
  "active": true,
  "editions": ["kindle", "hardback", "audio-book"],
  "authors": [
    {"name": "author1"},
    {"name": "author2"}
  ]
},
"books": ["book1", "book2"],
"publisher": [
  {"name": "pub1", "isbn": "slkdjfweoiru234"},
  {"name": "pub2", "isbn": "lksdjflkj3423"}
],
"parent": "projects/123456789",
"theme": "Kids",
"description": "This is a test description"
})"_json;
  std::string query_string;
  BuildQueryParamString(object, {"shelf", "parent", "publisher"}, &query_string);
  EXPECT_EQ(query_string, "books=book1&books=book2&description=This%20is%20a%20test%"
                          "20description&theme=Kids");

  query_string.clear();
  BuildQueryParamString(object, {"shelf.editions", "shelf.authors", "parent", "books", "publisher"},
                        &query_string);
  EXPECT_EQ(query_string, "description=This%20is%20a%20test%20description&shelf.active=true&"
                          "shelf.code=3&shelf.content=Some%20random%20content&shelf.name="
                          "science-fiction&theme=Kids");

  query_string.clear();
  BuildQueryParamString(object,
                        {"shelf.name", "shelf.code", "shelf.content", "shelf.active", "parent",
                         "theme", "description", "publisher"},
                        &query_string);
  EXPECT_EQ(query_string, "books=book1&books=book2&"
                          "shelf.authors.name=author1&shelf.authors.name=author2&"
                          "shelf.editions=kindle&shelf.editions=hardback&shelf.editions"
                          "=audio-book");

  query_string.clear();
  BuildQueryParamString(object,
                        {"shelf.name", "shelf.code", "shelf.content", "shelf.active", "parent",
                         "theme", "description"},
                        &query_string);
  EXPECT_EQ(query_string, "books=book1&books=book2&"
                          "publisher.isbn=slkdjfweoiru234&publisher.name=pub1&"
                          "publisher.isbn=lksdjflkj3423&publisher.name=pub2&"
                          "shelf.authors.name=author1&shelf.authors.name=author2&"
                          "shelf.editions=kindle&shelf.editions=hardback&shelf.editions"
                          "=audio-book");

  query_string.clear();
  BuildQueryParamString(object, {}, &query_string, "unit_test");
  EXPECT_EQ(query_string, "unit_test.books=book1&unit_test.books=book2&unit_test.description=This%"
                          "20is%20a%20test%20description&unit_test.parent=projects%2F123456789&"
                          "unit_test.publisher.isbn=slkdjfweoiru234&unit_test.publisher.name=pub1&"
                          "unit_test.publisher.isbn=lksdjflkj3423&unit_test.publisher.name=pub2&"
                          "unit_test.shelf.active=true&unit_test.shelf.authors.name=author1&unit_"
                          "test.shelf.authors.name=author2&unit_test.shelf.code=3&unit_test.shelf."
                          "content=Some%20random%20content&unit_test.shelf.editions=kindle&unit_"
                          "test.shelf.editions=hardback&unit_test.shelf.editions=audio-book"
                          "&unit_test.shelf.name=science-fiction&unit_test.theme=Kids");
}

TEST(BuildPath, ReturnsPathWithPlaceholdersReplaced) {
  nlohmann::json request = R"({
"shelf": {
  "name": "science-fiction",
  "code": 3,
  "content": "Some random content",
  "active": true,
  "editions": ["kindle", "hardback", "audio-book"],
  "authors": [
    {"name": "author1"},
    {"name": "author2"}
  ]
},
"books": ["book1", "book2"],
"parent": "projects/123456789",
"theme": "Kids",
"description": "This is a test description"
})"_json;

  // Check just the path whole message the request body.
  absl::StatusOr<std::string> path =
      BuildPath(request, "/v1/{parent=projects/*}/shelves/{shelf.name}", "*");
  ASSERT_TRUE(path.ok());
  EXPECT_EQ(path.value(), "/v1/projects/123456789/shelves/science-fiction");

  // Check the path with a single field as the request body.
  path = BuildPath(request, "/v1/{parent=projects/*}/shelves/{shelf.name}", "shelf");
  ASSERT_TRUE(path.ok());
  EXPECT_EQ(path.value(), "/v1/projects/123456789/shelves/"
                          "science-fiction?books=book1&books=book2&description=This%20is%20a%"
                          "20test%20description&theme=Kids");

  // Check path without any body fields.
  path = BuildPath(request, "/v1/{parent=projects/*}/shelves/{shelf.name}", "");
  ASSERT_TRUE(path.ok());
  EXPECT_EQ(path.value(), "/v1/projects/123456789/shelves/"
                          "science-fiction?books=book1&books=book2&"
                          "description=This%20is%20a%20test%20description&"
                          "shelf.active=true&shelf.authors.name=author1&"
                          "shelf.authors.name=author2&"
                          "shelf.code=3&shelf.content=Some%20random%20content&"
                          "shelf.editions=kindle&shelf.editions=hardback&"
                          "shelf.editions=audio-book"
                          "&theme=Kids");

  // Check the path with a single-segment path variable and a verb.
  path = BuildPath(request, "/v1/{parent}/shelves/{shelf.name}:custom_verb", "*");
  ASSERT_TRUE(path.ok());
  EXPECT_EQ(path.value(), "/v1/projects%2F123456789/shelves/science-fiction:custom_verb");

  // Check the path with a param key not found in the request message.
  path = BuildPath(request, "/v1/{parent=projects/*}/shelves/{name}", "*");
  ASSERT_FALSE(path.ok());

  path = BuildPath(request, "/v1/{parent/shelves", "*");
  ASSERT_FALSE(path.ok());
}

TEST(NestedJson, GetNestedJsonValuesAsString) {
  nlohmann::json request = R"({
"shelf": {
  "name": "science-fiction",
  "code": 3,
  "content": "Some random content",
  "active": true,
  "editions": ["kindle", "hardback", "audio-book"],
  "authors": [
    {"name": "author1"},
    {"name": "author2"}
  ]
},
"books": ["book1", "book2"],
"parent": "projects/123456789",
"theme": "Kids",
"description": "This is a test description"
})"_json;

  EXPECT_EQ(GetNestedJsonValueAsString(request, "shelf.code", true), "3");
  EXPECT_EQ(GetNestedJsonValueAsString(request, "shelf.active", true), "true");
  EXPECT_EQ(GetNestedJsonValueAsString(request, "description", true),
            "This%20is%20a%20test%20description");
}

} // namespace

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
