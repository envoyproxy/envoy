#pragma once

#include "extensions/filters/common/expr/context.h"

#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "common/http/headers.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using ExpressionPtr = std::unique_ptr<google::api::expr::runtime::CelExpression>;

// Creates an interpretable expression from a protobuf representation.
// Throws an exception if fails to construct a runtime expression.
ExpressionPtr create(const google::api::expr::v1alpha1::Expr& expr);

// Evaluates an expression for a request.
absl::optional<CelValue> evaluate(const google::api::expr::runtime::CelExpression& expr,
                  const StreamInfo::StreamInfo& info,
                  const Http::HeaderMap& headers);

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
