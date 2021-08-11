#include "source/common/formatter/substitution_formatter.h"

#include "test/common/formatter/substitution_formatter_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_PROTO_FUZZER(const test::common::substitution::TestCase& input) {
  try {
    TestUtility::validate(input);
    std::vector<Formatter::FormatterProviderPtr> formatters =
        Formatter::SubstitutionFormatParser::parse(input.format());
    const auto& request_headers =
        Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(input.request_headers());
    const auto& response_headers =
        Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(input.response_headers());
    const auto& response_trailers =
        Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(input.response_trailers());
    const std::unique_ptr<TestStreamInfo> stream_info = Fuzz::fromStreamInfo(input.stream_info());
    for (const auto& it : formatters) {
      it->format(request_headers, response_headers, response_trailers, *stream_info,
                 absl::string_view());
    }
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
