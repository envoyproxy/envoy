#include "extensions/tracers/xray/util.h"

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

bool WildcardMatch(absl::string_view pattern, absl::string_view input) {
  if (pattern.empty()) {
    return input.empty();
  }

  // Check the special case of a single * pattern, as it's common.
  constexpr char glob = '*';
  if (pattern.size() == 1 && pattern[0] == glob) {
    return true;
  }

  size_t i = 0, p = 0, iStar = input.size(), pStar = 0;
  while (i < input.size()) {
    if (p < pattern.size() && absl::ascii_tolower(input[i]) == absl::ascii_tolower(pattern[p])) {
      ++i;
      ++p;
    } else if (p < pattern.size() && '?' == pattern[p]) {
      ++i;
      ++p;
    } else if (p < pattern.size() && pattern[p] == glob) {
      iStar = i;
      pStar = p++;
    } else if (iStar != input.size()) {
      i = ++iStar;
      p = pStar + 1;
    } else
      return false;
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
