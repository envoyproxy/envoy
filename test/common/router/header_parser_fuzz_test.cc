#include "common/http/header_map_impl.h"
#include "common/router/header_parser.h"

#include "test/common/http/common_fuzz.h"
#include "test/common/router/header_parser_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {

using Envoy::replaceInvalidCharacters;

// Remove invalid characters from HeaderValueOptions in headers to add.
Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> processHeadersToAdd(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption>& headers_to_add) {
  Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValueOption> processed;
  for (auto header : headers_to_add) {
    auto* header_value_option = processed.Add();
    auto* mutable_header = header_value_option->mutable_header();
    mutable_header->set_key(replaceInvalidCharacters(header.header().key()));
    mutable_header->set_value(replaceInvalidCharacters(header.header().value()));
    header_value_option->mutable_append()->CopyFrom(header.append());
  }
  return processed;
}

DEFINE_PROTO_FUZZER(const test::common::router::TestCase& input) {
  try {
    MessageUtil::validate(input);
    auto headers_to_add = processHeadersToAdd(input.headers_to_add());
    Protobuf::RepeatedPtrField<std::string> headers_to_remove;
    for (auto s : input.headers_to_remove()) {
      headers_to_remove.Add(replaceInvalidCharacters(s));
    }
    Router::HeaderParserPtr parser =
        Router::HeaderParser::configure(headers_to_add, headers_to_remove);
    Http::HeaderMapImpl header_map;
    parser->evaluateHeaders(header_map, fromStreamInfo(input.stream_info()));
    ENVOY_LOG_MISC(trace, "Success");
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
