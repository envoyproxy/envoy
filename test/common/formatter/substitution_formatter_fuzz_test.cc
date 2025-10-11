#include <memory>

#include "source/common/formatter/substitution_formatter.h"

#include "test/common/formatter/substitution_formatter_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {
namespace {

DEFINE_PROTO_FUZZER(const test::common::substitution::TestCase& input) {
  // Create formatter context.
  Http::RequestHeaderMapPtr request_headers;
  Http::ResponseHeaderMapPtr response_headers;
  Http::ResponseTrailerMapPtr response_trailers;
  std::unique_ptr<StreamInfo::StreamInfo> stream_info;
  MockTimeSystem time_system;

  try {
    TestUtility::validate(input);
    request_headers = std::make_unique<Http::TestRequestHeaderMapImpl>(
        Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(input.request_headers()));
    response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
        Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(input.response_headers()));
    response_trailers = std::make_unique<Http::TestResponseTrailerMapImpl>(
        Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(input.response_trailers()));
    stream_info = Fuzz::fromStreamInfo(input.stream_info(), time_system);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "Creating formatter context failed, EnvoyException: {}", e.what());
    return;
  }

  Formatter::HttpFormatterContext formatter_context;
  formatter_context.setRequestHeaders(*request_headers)
      .setResponseHeaders(*response_headers)
      .setResponseTrailers(*response_trailers);

  // Text formatter.
  {
    Formatter::FormatterPtr formatter;
    try {
      auto formatter_or_error = Formatter::FormatterImpl::create(input.format(), false);
      if (!formatter_or_error.status().ok()) {
        ENVOY_LOG_MISC(debug, "TEXT formatter failed, EnvoyException: {}",
                       formatter_or_error.status().message());
        return;
      }
      formatter = std::move(*formatter_or_error);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "TEXT formatter failed, EnvoyException: {}", e.what());
      return;
    }

    // This should never throw.
    formatter->formatWithContext(formatter_context, *stream_info);
    ENVOY_LOG_MISC(trace, "TEXT formatter Success");
  }

  // JSON formatter.
  {

    Formatter::FormatterPtr formatter_keep_empty;
    Formatter::FormatterPtr formatter_omit_empty;

    try {
      // Create struct for JSON formatter.
      Protobuf::Struct struct_for_json_formatter;
      TestUtility::loadFromYaml(fmt::format(R"EOF(
      may_empty_a: '%REQ(may_empty)%'
      raw_bool_value: true
      raw_nummber_value: 6
      nested_list:
        - '%REQ(may_empty)%'
        - 14
        - "3.14"
        - false
        - "ok"
        - '%REQ(key_1)%'
        - '%REQ(error)%'
        - {}
        -'%REQ(may_empty)%'
      request_duration: '%REQUEST_DURATION%'
      may_empty_f: '%REQ(may_empty)%'
      nested_level:
        may_empty_c: '%REQ(may_empty)%'
        plain_string: plain_string_value
        may_empty_e: '%REQ(may_empty)%'
        protocol: '%PROTOCOL%'
        fuzz_format: {}
        may_empty_d: '%REQ(may_empty)%'
      request_key: '%REQ(key_1)%_@!!!_"_%REQ(key_2)%'
      may_empty_b: '%REQ(may_empty)%'
      )EOF",
                                            input.format(), input.format()),
                                struct_for_json_formatter);

      // Create JSON formatter.
      formatter_keep_empty =
          std::make_unique<Formatter::JsonFormatterImpl>(struct_for_json_formatter, false);
      formatter_omit_empty =
          std::make_unique<Formatter::JsonFormatterImpl>(struct_for_json_formatter, true);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "JSON formatter failed, EnvoyException: {}", e.what());
      return;
    }

    // This should never throw.
    const std::string keep_empty_result =
        formatter_keep_empty->formatWithContext(formatter_context, *stream_info);
    const std::string omit_empty_result =
        formatter_omit_empty->formatWithContext(formatter_context, *stream_info);

    // Ensure the result is legal JSON.
    Protobuf::Struct proto_struct;
    TestUtility::loadFromJson(keep_empty_result, proto_struct);
    TestUtility::loadFromJson(omit_empty_result, proto_struct);

    ENVOY_LOG_MISC(trace, "JSON formatter Success");
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
