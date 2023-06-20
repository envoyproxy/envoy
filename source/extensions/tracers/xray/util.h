#pragma once
#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

/**
 * Performs a case-insensitive wild-card match against the input string.
 * This method works with pseudo-regex chars; specifically ? and *
 *
 * An asterisk (*) represents any combination of characters.
 * A question mark (?) represents any single character.
 *
 * @param pattern The regex-like pattern to compare with.
 * @param text The string to compare against the pattern.
 * @return whether the text matches the pattern.
 */
bool wildcardMatch(absl::string_view pattern, absl::string_view input);

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
