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

DEFINE_PROTO_FUZZER(const test::extensions::filters::common::expr::EvaluatorTestCase& input) {
  // Create builder without constant folding.
  static Expr::BuilderPtr builder = Expr::createBuilder(nullptr);

  try {
    // Validate that the input has an expression.
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  // Create the headers and stream_info to test against.
  TestStreamInfo stream_info = Fuzz::fromStreamInfo(input.stream_info());
  Http::TestHeaderMapImpl request_headers = Fuzz::fromHeaders(input.request_headers());
  Http::TestHeaderMapImpl response_headers = Fuzz::fromHeaders(input.response_headers());
  Http::TestHeaderMapImpl response_trailers = Fuzz::fromHeaders(input.trailers());

  try {
    // Create the CEL expression.
    Expr::ExpressionPtr expr = Expr::createExpression(*builder, input.expression());

    // Evaluate the CEL expression.
    Protobuf::Arena arena;
    Expr::evaluate(*expr, arena, stream_info, &request_headers, &response_headers,
                   &response_trailers);
  } catch (const CelException& e) {
    ENVOY_LOG_MISC(debug, "CelException: {}", e.what());
  }
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
