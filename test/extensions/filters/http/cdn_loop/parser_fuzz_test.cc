#include "extensions/filters/http/cdn_loop/parser.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Envoy::Extensions::HttpFilters::CdnLoop::Parser::ParseContext input(
      absl::string_view(reinterpret_cast<const char*>(buf), len));
  Envoy::Extensions::HttpFilters::CdnLoop::Parser::parseCdnInfoList(input).IgnoreError();
}

} // namespace Fuzz
} // namespace Envoy
