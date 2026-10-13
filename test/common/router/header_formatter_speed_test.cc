#include "source/common/router/header_parser.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Router {

static void bmEvaluateHeaders(benchmark::State& state) {
  auto request_header = Http::RequestHeaderMapImpl::create();
  request_header->addCopy(Http::LowerCaseString("bar"), "a");
  request_header->addCopy(Http::LowerCaseString("foo"), 1);
  request_header->addCopy(Http::LowerCaseString("test1"), "to_overwrite");

  Event::SimulatedTimeSystem time_system;
  // Allocate empty stream_info. It is not really used, but HeaderParser::evaluateHeaders
  // does not invoke formatter when pointer to stream_info is null.
  const auto stream_info = std::make_unique<Envoy::TestStreamInfo>(time_system);

  // Prepare config with headers to add and overwrite.
  Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> headers_to_add;
  for (auto i = 1; i < state.range(0); i++) {
    envoy::config::core::v3::HeaderValueOption* header_value_option = headers_to_add.Add();
    auto* mutable_header = header_value_option->mutable_header();
    if ((i % 2) == 0) {
      header_value_option->set_append_action(HeaderValueOption::APPEND_IF_EXISTS_OR_ADD);
    } else {
      header_value_option->set_append_action(HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
    }
    mutable_header->set_key(fmt::format("test{}", i));
    // The value of the header to add is a static string. HeaderParser::configure will allocate a
    // simple formatter which does no real formatting and returns the same string.
    mutable_header->set_value("TEST");
  }

  // Instantiate HeaderParser
  HeaderParserPtr header_parser = HeaderParser::configure(headers_to_add).value();

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    header_parser->evaluateHeaders(*request_header, {request_header.get()}, *stream_info);
  }
}

BENCHMARK(bmEvaluateHeaders)->DenseRange(2, 20, 2);

// bmEvaluateHeaders uses APPEND_IF_EXISTS_OR_ADD for half of the headers, so the header map it
// evaluates against grows on every iteration and the measurement includes that growth. The two
// benchmarks below use OVERWRITE_IF_EXISTS_OR_ADD exclusively, which keeps the header map at a
// fixed size and isolates the cost of evaluating the headers themselves. They differ only in
// whether the header value is a constant or a substitution command.
static void bmEvaluateHeadersConstant(benchmark::State& state) {
  auto request_header = Http::RequestHeaderMapImpl::create();
  request_header->addCopy(Http::LowerCaseString("x-source"),
                          "0123456789012345678901234567890123456789");

  Event::SimulatedTimeSystem time_system;
  const auto stream_info = std::make_unique<Envoy::TestStreamInfo>(time_system);

  Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> headers_to_add;
  for (auto i = 1; i < state.range(0); i++) {
    envoy::config::core::v3::HeaderValueOption* header_value_option = headers_to_add.Add();
    auto* mutable_header = header_value_option->mutable_header();
    header_value_option->set_append_action(HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
    mutable_header->set_key(fmt::format("test{}", i));
    mutable_header->set_value("0123456789012345678901234567890123456789");
  }

  HeaderParserPtr header_parser = HeaderParser::configure(headers_to_add).value();

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    header_parser->evaluateHeaders(*request_header, {request_header.get()}, *stream_info);
  }
}

BENCHMARK(bmEvaluateHeadersConstant)->DenseRange(2, 20, 2);

// Same as bmEvaluateHeadersConstant, but every header value is a substitution command that has to
// be evaluated against the stream for each request. The source header value is deliberately longer
// than the std::string small-string buffer, which is the common case for real headers and the case
// where copying the formatted value out of the formatter is most expensive.
static void bmEvaluateHeadersDynamic(benchmark::State& state) {
  auto request_header = Http::RequestHeaderMapImpl::create();
  request_header->addCopy(Http::LowerCaseString("x-source"),
                          "0123456789012345678901234567890123456789");

  Event::SimulatedTimeSystem time_system;
  const auto stream_info = std::make_unique<Envoy::TestStreamInfo>(time_system);

  Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption> headers_to_add;
  for (auto i = 1; i < state.range(0); i++) {
    envoy::config::core::v3::HeaderValueOption* header_value_option = headers_to_add.Add();
    auto* mutable_header = header_value_option->mutable_header();
    header_value_option->set_append_action(HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
    mutable_header->set_key(fmt::format("test{}", i));
    mutable_header->set_value("%REQ(x-source)%");
  }

  HeaderParserPtr header_parser = HeaderParser::configure(headers_to_add).value();

  for (auto _ : state) { // NOLINT: Silences warning about dead store
    header_parser->evaluateHeaders(*request_header, {request_header.get()}, *stream_info);
  }
}

BENCHMARK(bmEvaluateHeadersDynamic)->DenseRange(2, 20, 2);

} // namespace Router
} // namespace Envoy
