#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"

#include "test/common/router/header_parser_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_PROTO_FUZZER(const test::common::router::TestCase& input) {
  try {
    TestUtility::validate(input);
    Router::HeaderParserPtr parser =
        Router::HeaderParser::configure(input.headers_to_add(), input.headers_to_remove());
    Http::TestRequestHeaderMapImpl request_header_map;
    Http::TestResponseHeaderMapImpl response_header_map;
    MockTimeSystem time_system_;
    std::unique_ptr<TestStreamInfo> test_stream_info =
        fromStreamInfo(input.stream_info(), time_system_);
    parser->evaluateHeaders(request_header_map, {&request_header_map, &response_header_map},
                            *test_stream_info);
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
