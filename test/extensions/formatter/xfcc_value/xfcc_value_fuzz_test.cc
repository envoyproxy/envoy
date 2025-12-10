#include "source/common/common/logger.h"
#include "source/common/formatter/http_formatter_context.h"
#include "source/extensions/formatter/xfcc_value/xfcc_value.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Http::HeaderStringValidator::disable_validation_for_tests_ = true;
  absl::string_view sv(reinterpret_cast<const char*>(buf), len);

  // We just want to make sure that the parser doesn't crash with any input.
  XfccValueFormatterCommandParser parser;
  auto formatter = parser.parse("XFCC_VALUE", "uri", absl::nullopt);

  Http::TestRequestHeaderMapImpl request_headers{};
  request_headers.setForwardedClientCert(sv);
  StreamInfo::MockStreamInfo stream_info;

  formatter->formatValue({&request_headers}, stream_info);
}

} // namespace
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
