#pragma once

#include "envoy/stream_info/stream_info.h"

#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/expr/context.h"

#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using Expression = google::api::expr::runtime::CelExpression;
using ExpressionPtr = std::unique_ptr<Expression>;

// Creates an interpretable expression from a protobuf representation.
// Throws an exception if fails to construct a runtime expression.
ExpressionPtr create(const google::api::expr::v1alpha1::Expr& expr);

// Evaluates an expression for a request. The arena is used to hold intermediate computational
// results and potentially the final value.
absl::optional<CelValue> evaluate(const Expression& expr, Protobuf::Arena* arena,
                                  const StreamInfo::StreamInfo& info,
                                  const Http::HeaderMap& headers);

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
