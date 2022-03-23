#include <string>

#include "source/common/json/json_sanitizer.h"

#include "test/common/json/json_sanitizer_test_util.h"

namespace Envoy {
namespace Json {

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    return str.substr(1, str.size() - 2);
  }
  return str;
}

} // namespace Json
} // namespace Envoy
