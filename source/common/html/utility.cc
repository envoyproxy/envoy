#include "source/common/html/utility.h"

#include <string>

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Html {

std::string Utility::sanitize(absl::string_view text) {
  return absl::StrReplaceAll(
      text, {{"&", "&amp;"}, {"<", "&lt;"}, {">", "&gt;"}, {"\"", "&quot;"}, {"'", "&#39;"}});
}

} // namespace Html
} // namespace Envoy
