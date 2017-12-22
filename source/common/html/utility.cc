#include "common/html/utility.h"

#include <string>

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Html {

std::string Utility::sanitize(const std::string& text) {
  return absl::StrReplaceAll(
      text, {{"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"}, {"\"", "&quot;"}, {"'", "&#39;"}});
}

} // namespace Html
} // namespace Envoy
