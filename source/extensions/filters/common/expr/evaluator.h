#pragma once

#include "envoy/stream_info/stream_info.h"

#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"

// CEL-CPP does not enforce unused parameter checks consistently, so we relax it here.

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "eval/public/activation.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

using Activation = google::api::expr::runtime::BaseActivation;
using ActivationPtr = std::unique_ptr<Activation>;
using Builder = google::api::expr::runtime::CelExpressionBuilder;
using BuilderPtr = std::unique_ptr<Builder>;
using Expression = google::api::expr::runtime::CelExpression;
using ExpressionPtr = std::unique_ptr<Expression>;

// Base class for the context used by the CEL evaluator to look up attributes.
class StreamActivation : public google::api::expr::runtime::BaseActivation {
public:
  StreamActivation(const ::Envoy::LocalInfo::LocalInfo* local_info,
                   const StreamInfo::StreamInfo& info,
                   const ::Envoy::Http::RequestHeaderMap* request_headers,
                   const ::Envoy::Http::ResponseHeaderMap* response_headers,
                   const ::Envoy::Http::ResponseTrailerMap* response_trailers)
      : local_info_(local_info), activation_info_(&info),
        activation_request_headers_(request_headers),
        activation_response_headers_(response_headers),
        activation_response_trailers_(response_trailers) {}

  StreamActivation() = default;

  absl::optional<CelValue> FindValue(absl::string_view name, Protobuf::Arena* arena) const override;
  std::vector<const google::api::expr::runtime::CelFunction*>
  FindFunctionOverloads(absl::string_view) const override {
    return {};
  }

protected:
  void resetActivation() const;
  mutable const ::Envoy::LocalInfo::LocalInfo* local_info_{nullptr};
  mutable const StreamInfo::StreamInfo* activation_info_{nullptr};
  mutable const ::Envoy::Http::RequestHeaderMap* activation_request_headers_{nullptr};
  mutable const ::Envoy::Http::ResponseHeaderMap* activation_response_headers_{nullptr};
  mutable const ::Envoy::Http::ResponseTrailerMap* activation_response_trailers_{nullptr};
};

// Creates an activation providing the common context attributes.
// The activation lazily creates wrappers during an evaluation using the evaluation arena.
ActivationPtr createActivation(const ::Envoy::LocalInfo::LocalInfo* local_info,
                               const StreamInfo::StreamInfo& info,
                               const ::Envoy::Http::RequestHeaderMap* request_headers,
                               const ::Envoy::Http::ResponseHeaderMap* response_headers,
                               const ::Envoy::Http::ResponseTrailerMap* response_trailers);

// Shared expression builder instance.
class BuilderInstance : public Singleton::Instance {
public:
  explicit BuilderInstance(BuilderPtr builder) : builder_(std::move(builder)) {}
  Builder& builder() { return *builder_; }

private:
  BuilderPtr builder_;
};

using BuilderInstanceSharedPtr = std::shared_ptr<BuilderInstance>;

// Creates an expression builder. The optional arena is used to enable constant folding
// for intermediate evaluation results.
// Throws an exception if fails to construct an expression builder.
BuilderPtr createBuilder(Protobuf::Arena* arena);

// Gets the singleton expression builder. Must be called on the main thread.
BuilderInstanceSharedPtr getBuilder(Server::Configuration::CommonFactoryContext& context);

// Creates an interpretable expression from a protobuf representation.
// Throws an exception if fails to construct a runtime expression.
ExpressionPtr createExpression(Builder& builder, const google::api::expr::v1alpha1::Expr& expr);

// Evaluates an expression for a request. The arena is used to hold intermediate computational
// results and potentially the final value.
absl::optional<CelValue> evaluate(const Expression& expr, Protobuf::Arena& arena,
                                  const ::Envoy::LocalInfo::LocalInfo* local_info,
                                  const StreamInfo::StreamInfo& info,
                                  const ::Envoy::Http::RequestHeaderMap* request_headers,
                                  const ::Envoy::Http::ResponseHeaderMap* response_headers,
                                  const ::Envoy::Http::ResponseTrailerMap* response_trailers);

// Evaluates an expression and returns true if the expression evaluates to "true".
// Returns false if the expression fails to evaluate.
bool matches(const Expression& expr, const StreamInfo::StreamInfo& info,
             const ::Envoy::Http::RequestHeaderMap& headers);

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
