#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {

// Access to a request payload document by JSON pointer (RFC 6901). None of these throw: they run on
// worker threads, where Envoy does not use exceptions.

// Serializes the value `pointer` names. InvalidArgument for a malformed pointer, NotFound when it
// names no value, and FailedPrecondition when the value is or holds an offloaded string.
absl::StatusOr<std::string> getJson(const nlohmann::json& document, absl::string_view pointer);

// Writes `value`, parsed as JSON, where `pointer` names: replaces a value, adds an object member,
// or appends to an array for a last token of "-". InvalidArgument for a malformed pointer or value,
// or for a root that would not be an object; NotFound when the parent names no container.
absl::Status setJson(nlohmann::json& document, absl::string_view pointer, absl::string_view value);

// Removes the object member or array element `pointer` names. InvalidArgument for a malformed
// pointer or the root; NotFound when it names no value.
absl::Status removeJson(nlohmann::json& document, absl::string_view pointer);

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
