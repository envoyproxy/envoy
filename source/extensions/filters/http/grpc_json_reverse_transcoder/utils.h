#pragma once

#include <optional>
#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

inline constexpr char reserved_chars[] = " %:?#[]@!$&'()*+,;=";

// This builds grpc-message header value from body data.
std::string BuildGrpcMessage(Envoy::Buffer::Instance& body_data);

// Takes the json object and a path and returns the value of the json object at
// the path. Example: If the json object is {"a": {"b": 2}, "c": 2} and the path
// is "a.b", this will return "2".
// All the characters in the value of a path variables with single segments are
// percent encoded, except [-_.0-9a-zA-Z]; and all the characters in the value
// of a path variables with multiple segments are percent encoded, except
// [-_./0-9a-zA-Z].
// TODO(numanelahi): Add `~` to the list of characters that are not percent
// encoded.
absl::optional<std::string> GetNestedJsonValueAsString(const nlohmann::json& object,
                                                       const std::string& key,
                                                       bool has_one_path_segment);

// Takes the json object builds a percent encoded query string out of it.
// @param object The json object to build the query string from.
// @param param_set The set of keys to ignore from the object.
// @param query_string pointer to the query string variable.
// @param key_prefix The prefix to add to the keys.
//
// Example 1: object = {
//   "a": {
//      "b": 2
//      "c": 4
//   },
//   "d": 2
//   "e": "hello world"
// }
// and param_set = {"a.b", "d"}
// then query_string = "a.c=4&e=hello%20world"
//
// Example 2: object = {
//   "a": {
//      "b": 2
//      "c": 4
//      "d": "hello"
//   },
//   "e": 2
//   "f": "hello world"
//   "g": 1.234
// },
// param_set = {"a", "e"}, and
// key_prefix = "prefix"
// then query_string = "prefix.f=hello%20world&prefix.g=1.234"
void BuildQueryParamString(const nlohmann::json& object,
                           const absl::flat_hash_set<std::string>& ignore_list,
                           std::string* query_string, std::string key_prefix = "");

// Takes the request json object and the gRPC method's http path annotation and
// build the normalized HTTP path out of it.
// Example: For the json object below
// {
//   "shelf": {
//     "name": "fiction",
//     "code": 3,
//     "content": "Some random content",
//     "active": true
//   },
//   "parent": "projects/123456789",
//   "theme": "Kids",
//   "description": "This is a test description"
// }
// and the path annotation "/v1/{parent=projects/*}/shelves/{shelf.name}", this
// function will return `/v1/projects/123456789/shelves/fiction`, when the
// http_body_field is "*" and
// `/v1/projects/123456789/shelves/fiction?description=This%20is%20a%20test%20description&theme=Kids`,
// when the http_body_field is "shelf", adding `theme` and `description` as
// query parameters to the http path.
absl::StatusOr<std::string> BuildPath(nlohmann::json& request, std::string http_rule_path,
                                      std::string http_body_field);

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
