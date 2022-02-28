#include "source/common/network/utility.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/extended_request_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "test/common/stream_info/test_util.h"
#include "test/extensions/filters/common/expr/evaluator_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace {

using CustomCel::ExtendedRequest::ExtendedRequestCelVocabulary;
using test::extensions::filters::common::expr::EvaluatorTestCase;

void protoFuzzer(const EvaluatorTestCase& input, bool use_custom_cel_vocabulary) {
  // Create builder without constant folding.
  ExtendedRequestCelVocabulary custom_cel_vocabulary(true);
  static Expr::BuilderPtr builder;
  if (use_custom_cel_vocabulary) {
    builder = Expr::createBuilder(nullptr, &custom_cel_vocabulary);
  } else {
    builder = Expr::createBuilder(nullptr);
  }
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
    // Create the CEL expression.
    Expr::ExpressionPtr expr = Expr::createExpression(*builder, input.expression());

    // Evaluate the CEL expression.
    Protobuf::Arena arena;
    if (use_custom_cel_vocabulary) {
      Expr::evaluate(*expr, arena, *stream_info, &request_headers, &response_headers,
                     &response_trailers, &custom_cel_vocabulary);
    } else {
      Expr::evaluate(*expr, arena, *stream_info, &request_headers, &response_headers,
                     &response_trailers);
    }
  } catch (const CelException& e) {
    ENVOY_LOG_MISC(debug, "CelException: {}", e.what());
  }
}

DEFINE_PROTO_FUZZER(const test::extensions::filters::common::expr::EvaluatorTestCase& input) {
  protoFuzzer(input, false);
  protoFuzzer(input, true);
}

} // namespace
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
