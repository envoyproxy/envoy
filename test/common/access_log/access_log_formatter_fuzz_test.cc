#include "common/access_log/access_log_formatter.h"

#include "test/common/access_log/access_log_formatter_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {

DEFINE_PROTO_FUZZER(const test::common::access_log::TestCase& input) {
  try {
    std::vector<AccessLog::FormatterPtr> formatters =
        AccessLog::AccessLogFormatParser::parse(input.format());
    for (const auto& it : formatters) {
      it->format(Fuzz::fromHeaders(input.request_headers()),
                 Fuzz::fromHeaders(input.response_headers()),
                 Fuzz::fromHeaders(input.response_trailers()),
                 Fuzz::fromRequestInfo(input.request_info()));
    }
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace Fuzz
} // namespace Envoy
