#include "common/http/http1/header_formatter.h"

#include <string>

namespace Envoy {
namespace Http {
namespace Http1 {
std::string ProperCaseHeaderKeyFormatter::format(absl::string_view key) const {
  auto copy = std::string(key);

  bool should_capitalize = true;
  for (char& c : copy) {
    if (should_capitalize && isalpha(c)) {
      c = static_cast<char>(toupper(c));
    }

    should_capitalize = !isalpha(c) && !isdigit(c);
  }

  return copy;
}

std::string CustomHeaderKeyFormatter::format(absl::string_view key) const {
  auto copy = std::string(key);

  // Check for a custom header key rewrite
  const auto &rewrite = rules_.find(copy);
  if (rewrite != rules_.end()) {
    // Return a copy of the rewrite.
    return rewrite->second;
  }

  // Return a copy of the original key
  return copy;
}

} // namespace Http1
} // namespace Http
} // namespace Envoy
