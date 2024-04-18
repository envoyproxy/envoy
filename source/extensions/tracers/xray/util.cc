#include "source/extensions/tracers/xray/util.h"

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

// AWS X-Ray has two sources of sampling rules (wildcard patterns):
//
// 1- The manifest file (static config)
// 2- The X-Ray Service - periodically polled for new rules. (dynamic config)
//
// X-Ray will inspect every single request and try to match it against the set of rules it has to
// decide whether or not a given request should be sampled. That means this wildcard matching
// routine is on the hot path. I've spent a great deal of time to make this function optimal and not
// allocate.
// Using regex matching here has many downsides and it would require us to:
// 1- escape/lint the user input pattern to avoid messing up its meaning (think '.' character is a
// valid regex and URL character) 2- compile the regex and store it with every corresponding part of
// the rule
//
// Those two steps would add significant overhead to the tracer. Meanwhile, the following function
// has a comprehensive test suite and fuzz tests.
bool wildcardMatch(absl::string_view pattern, absl::string_view input) {
  if (pattern.empty()) {
    return input.empty();
  }

  // Check the special case of a single * pattern, as it's common.
  constexpr char glob = '*';
  if (pattern.size() == 1 && pattern[0] == glob) {
    return true;
  }

  size_t i = 0, p = 0, i_star = input.size(), p_star = 0;
  while (i < input.size()) {
    if (p < pattern.size() && absl::ascii_tolower(input[i]) == absl::ascii_tolower(pattern[p])) {
      ++i;
      ++p;
    } else if (p < pattern.size() && '?' == pattern[p]) {
      ++i;
      ++p;
    } else if (p < pattern.size() && pattern[p] == glob) {
      i_star = i;
      p_star = p++;
    } else if (i_star != input.size()) {
      i = ++i_star;
      p = p_star + 1;
    } else {
      return false;
    }
  }

  while (p < pattern.size() && pattern[p] == glob) {
    ++p;
  }

  return p == pattern.size() && i == input.size();
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
