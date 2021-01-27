#pragma once

#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {

/**
 * Path canonicalizer based on //source/common/chromium_url.
 */
class LegacyPathCanonicalizer {
public:
  // Returns the canonicalized path if successful.
  static absl::optional<std::string> canonicalizePath(absl::string_view original_path);
};

} // namespace Http
} // namespace Envoy
