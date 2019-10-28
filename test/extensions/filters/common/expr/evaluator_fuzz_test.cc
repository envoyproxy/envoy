#include "common/network/utility.h"

#include "extensions/filters/common/expr/evaluator.h"

#include "test/common/stream_info/test_util.h"
#include "test/extensions/filters/common/expr/evaluator_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

// Set stream_info using information from input.
// Duplicates most of fromStreamInfo, except is more lightweight without the use of mocks.
TestStreamInfo makeStreamInfo(const test::fuzz::StreamInfo& info) {
  TestStreamInfo stream_info;
  // TODO(asraa): Set upstreamHost() and downstreamSslConnection().
  stream_info.metadata_ = info.dynamic_metadata();
  const auto start_time =
      static_cast<uint64_t>(std::numeric_limits<std::chrono::nanoseconds::rep>::max()) <
              info.start_time()
          ? 0
          : info.start_time() / 1000;
  stream_info.start_time_ = SystemTime(std::chrono::microseconds(start_time));
  stream_info.setRequestedServerName(info.requested_server_name());
  stream_info.response_code_ = info.has_response_code() ? info.response_code().value() : 200;
  auto address = Network::Utility::resolveUrl("tcp://10.0.0.1:443");
  stream_info.upstream_local_address_ = address;
  stream_info.downstream_local_address_ = address;
  stream_info.downstream_direct_remote_address_ = address;
  stream_info.downstream_remote_address_ = address;
  return stream_info;
}

DEFINE_PROTO_FUZZER(const test::extensions::filters::common::expr::EvaluatorTestCase& input) {
  // Create builder without constant folding.
  static Expr::BuilderPtr builder = Expr::createBuilder(nullptr);

  try {
    // Validate that the input has an expression.
    TestUtility::validate(input);

    // Create the CEL expression.
    Expr::ExpressionPtr expr = Expr::createExpression(*builder, input.expression());

    // Create the headers and stream_info to test against.
    TestStreamInfo stream_info = makeStreamInfo(input.stream_info());
    Http::TestHeaderMapImpl request_headers = Fuzz::fromHeaders(input.request_headers());
    Http::TestHeaderMapImpl response_headers = Fuzz::fromHeaders(input.response_headers());
    Http::TestHeaderMapImpl response_trailers = Fuzz::fromHeaders(input.trailers());

    // Evaluate the CEL expression.
    Protobuf::Arena arena;
    Expr::evaluate(*expr, nullptr, stream_info, &request_headers, &response_headers,
                   &response_trailers);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
  }
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
