#include "common/http/http1/header_formatter.h"

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
} // namespace Http1
} // namespace Http
} // namespace Envoy