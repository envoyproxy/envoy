#include "source/common/common/statusor.h"
#include "source/extensions/path/uri_template_lib/uri_template.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  absl::string_view input(reinterpret_cast<const char*>(buf), len);

  // Test rewriter parser.
  absl::StatusOr<std::vector<Envoy::Extensions::UriTemplate::ParsedSegment>> rewrite =
      Envoy::Extensions::UriTemplate::parseRewritePattern(input);

  // Test matcher parser.
  absl::StatusOr<absl::string_view> match =
      Envoy::Extensions::UriTemplate::convertPathPatternSyntaxToRegex(input);
}

} // namespace Fuzz
} // namespace Envoy
