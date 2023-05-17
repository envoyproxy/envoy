#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace DirectResponseTesting {

// The match operation to perform.
enum class MatchMode { Contains, Exact, Prefix, Suffix };

// A configuration for when a header should be matched.
struct HeaderMatcher {
  std::string name;
  std::string value;
  MatchMode mode;
};

// A configuration for when a route should be matched.
struct RouteMatcher {
  std::string fullPath;
  std::string pathPrefix;
  std::vector<HeaderMatcher> headers;
};

// A direct response to configure for testing.
struct DirectResponse {
  RouteMatcher matcher;
  unsigned int status;
  std::string body;
  absl::flat_hash_map<std::string, std::string> headers;
};

} // namespace DirectResponseTesting
} // namespace Envoy
