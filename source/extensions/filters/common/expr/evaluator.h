#pragma once

#include "envoy/stream_info/stream_info.h"

#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"

#include "eval/public/activation.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using Activation = google::api::expr::runtime::Activation;
using ActivationPtr = std::unique_ptr<Activation>;
using Builder = google::api::expr::runtime::CelExpressionBuilder;
using BuilderPtr = std::unique_ptr<Builder>;
using Expression = google::api::expr::runtime::CelExpression;
using ExpressionPtr = std::unique_ptr<Expression>;

using CelValue = google::api::expr::runtime::CelValue;
using CustomCelVocabulary =
    Envoy::Extensions::Filters::Common::Expr::Custom_Cel::CustomCelVocabulary;

// Creates an activation providing the common context attributes.
// The activation lazily creates wrappers during an evaluation using the evaluation arena.
ActivationPtr createActivation(Protobuf::Arena& arena, const StreamInfo::StreamInfo& info,
                               const Http::RequestHeaderMap* request_headers,
                               const Http::ResponseHeaderMap* response_headers,
                               const Http::ResponseTrailerMap* response_trailers,
                               CustomCelVocabulary* custom_cel_vocabulary);

// Creates an expression builder. The optional arena is used to enable constant folding
// for intermediate evaluation results.
// Throws an exception if fails to construct an expression builder.
BuilderPtr createBuilder(Protobuf::Arena* arena, const CustomCelVocabulary* custom_cel_vocabulary);

BuilderPtr createBuilder(Protobuf::Arena* arena);

// Creates an interpretable expression from a protobuf representation.
// Throws an exception if fails to construct a runtime expression.
ExpressionPtr createExpression(Builder& builder, const google::api::expr::v1alpha1::Expr& expr);

// Evaluates an expression for a request. The arena is used to hold intermediate computational
// results and potentially the final value.
absl::optional<CelValue> evaluate(const Expression& expr, Protobuf::Arena& arena,
                                  const StreamInfo::StreamInfo& info,
                                  const Http::RequestHeaderMap* request_headers,
                                  const Http::ResponseHeaderMap* response_headers,
                                  const Http::ResponseTrailerMap* response_trailers);

// Same as above. Takes a parameter for a custom_cel_vocabulary.
absl::optional<CelValue> evaluate(const Expression& expr, Protobuf::Arena& arena,
                                  const StreamInfo::StreamInfo& info,
                                  const Http::RequestHeaderMap* request_headers,
                                  const Http::ResponseHeaderMap* response_headers,
                                  const Http::ResponseTrailerMap* response_trailers,
                                  CustomCelVocabulary* custom_cel_vocabulary);

// Evaluates an expression and returns true if the expression evaluates to "true".
// Returns false if the expression fails to evaluate.
bool matches(const Expression& expr, const StreamInfo::StreamInfo& info,
             const Http::RequestHeaderMap& headers);

// Same as above. Takes a parameter for a custom_cel_vocabulary.
bool matches(const Expression& expr, const StreamInfo::StreamInfo& info,
             const Http::RequestHeaderMap& headers, CustomCelVocabulary* custom_cel_vocabulary);

// Returns a string for a CelValue.
std::string print(CelValue value);

// Thrown when there is an CEL library error.
class CelException : public EnvoyException {
public:
  CelException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
