#include "source/common/network/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/common/stream_info/test_util.h"
#include "test/extensions/filters/common/expr/evaluator_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "cel/expr/syntax.pb.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

DEFINE_PROTO_FUZZER(const test::extensions::filters::common::expr::EvaluatorTestCase& input) {
  // Create builder without constant folding.
  static auto builder = Expr::createBuilder(nullptr);
  MockTimeSystem time_source;
  std::unique_ptr<TestStreamInfo> stream_info;

  try {
    // Validate that the input has an expression.
    TestUtility::validate(input);
    // Create stream_info to test against, this may catch exceptions from invalid addresses.
    stream_info = Fuzz::fromStreamInfo(input.stream_info(), time_source);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }

  auto request_headers = Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(input.request_headers());
  auto response_headers =
      Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(input.response_headers());
  auto response_trailers = Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(input.trailers());

  try {
    // Create the CEL expression with boundary conversion.
    std::string serialized;
    if (!input.expression().SerializeToString(&serialized)) {
      ENVOY_LOG_MISC(debug, "Failed to serialize expression");
      return;
    }
    cel::expr::Expr new_expr;
    if (!new_expr.ParseFromString(serialized)) {
      ENVOY_LOG_MISC(debug, "Failed to convert expression to new format");
      return;
    }
    auto expr = Expr::CompiledExpression::Create(builder, new_expr);
    if (!expr.ok()) {
      ENVOY_LOG_MISC(debug, "Failed to compile");
      return;
    }

    // Evaluate the CEL expression.
    Protobuf::Arena arena;
    expr->evaluate(arena, nullptr, *stream_info, &request_headers, &response_headers,
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
