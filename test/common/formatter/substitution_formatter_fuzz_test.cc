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

  const Formatter::HttpFormatterContext formatter_context{
      request_headers.get(), response_headers.get(), response_trailers.get()};

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

    Formatter::FormatterPtr formatter;
    Formatter::FormatterPtr typed_formatter;

    try {
      // Create struct for JSON formatter.
      ProtobufWkt::Struct struct_for_json_formatter;
      TestUtility::loadFromYaml(fmt::format(R"EOF(
      raw_bool_value: true
      raw_nummber_value: 6
      nested_list:
        - 14
        - "3.14"
        - false
        - "ok"
        - '%REQ(key_1)%'
        - '%REQ(error)%'
        - {}
      request_duration: '%REQUEST_DURATION%'
      nested_level:
        plain_string: plain_string_value
        protocol: '%PROTOCOL%'
        fuzz_format: {}
      request_key: '%REQ(key_1)%_@!!!_"_%REQ(key_2)%'
      )EOF",
                                            input.format(), input.format()),
                                struct_for_json_formatter);

      // Create JSON formatter.
      formatter = std::make_unique<Formatter::JsonFormatterImpl>(struct_for_json_formatter, false);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "JSON formatter failed, EnvoyException: {}", e.what());
      return;
    }

    // This should never throw.
    formatter->formatWithContext(formatter_context, *stream_info);
    typed_formatter->formatWithContext(formatter_context, *stream_info);
    ENVOY_LOG_MISC(trace, "JSON formatter Success");
  }
}

} // namespace
} // namespace Fuzz
} // namespace Envoy
