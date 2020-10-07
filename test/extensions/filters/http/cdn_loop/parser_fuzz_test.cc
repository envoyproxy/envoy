#include "common/common/statusor.h"

#include "extensions/filters/http/cdn_loop/parser.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

using Envoy::Extensions::HttpFilters::CdnLoop::Parser::parseCdnInfoList;
using Envoy::Extensions::HttpFilters::CdnLoop::Parser::ParseContext;
using Envoy::Extensions::HttpFilters::CdnLoop::Parser::ParsedCdnInfoList;

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  absl::string_view input(reinterpret_cast<const char*>(buf), len);
  StatusOr<ParsedCdnInfoList> list = parseCdnInfoList(ParseContext(input));
  if (list) {
    // If we successfully parse input, we should make sure that cdn_ids we find appear in the input
    // string in order.
    size_t start = 0;
    for (const absl::string_view& cdn_id : list->cdnIds()) {
      size_t pos = input.find(cdn_id, start);
      FUZZ_ASSERT(pos != absl::string_view::npos);
      FUZZ_ASSERT(pos >= start);
      start = pos + cdn_id.length();
    }
  }
}

} // namespace Fuzz
} // namespace Envoy
