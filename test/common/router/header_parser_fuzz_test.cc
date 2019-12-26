#include "common/http/header_map_impl.h"
#include "common/router/header_parser.h"

#include "test/common/router/header_parser_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_PROTO_FUZZER(const test::common::router::TestCase& input) {
  try {
    TestUtility::validate(input);
    auto headers_to_add = replaceInvalidHeaders(input.headers_to_add());
    Protobuf::RepeatedPtrField<std::string> headers_to_remove;
    for (const auto& s : input.headers_to_remove()) {
      headers_to_remove.Add(replaceInvalidCharacters(s));
    }
    Router::HeaderParserPtr parser =
        Router::HeaderParser::configure(headers_to_add, headers_to_remove);
    Http::HeaderMapImpl header_map;
    TestStreamInfo test_stream_info = fromStreamInfo(input.stream_info());
    parser->evaluateHeaders(header_map, test_stream_info);
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
