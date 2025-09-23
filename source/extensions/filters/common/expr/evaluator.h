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

#include "xds/type/v3/cel.pb.h"
#include "cel/expr/syntax.pb.h"

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
using BuilderConstPtr = std::unique_ptr<const Builder>;
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
  explicit BuilderInstance(BuilderConstPtr builder) : builder_(std::move(builder)) {}
  const Builder& builder() const { return *builder_; }

private:
  const BuilderConstPtr builder_;
};

using BuilderInstanceSharedPtr = std::shared_ptr<BuilderInstance>;
using BuilderInstanceSharedConstPtr = std::shared_ptr<const BuilderInstance>;

// Creates an expression builder. The optional arena is used to enable constant folding
// for intermediate evaluation results.
// Throws an exception if fails to construct an expression builder.
BuilderInstanceSharedPtr createBuilder(Protobuf::Arena* arena);

// Gets the singleton expression builder. Must be called on the main thread.
BuilderInstanceSharedConstPtr getBuilder(Server::Configuration::CommonFactoryContext& context);

// Compiled CEL expression. This class ensures both the builder and the source expression outlive
// the compiled expression.
class CompiledExpression {
public:
  // Creates an interpretable expression from the new CEL expr format, making a copy of it.
  static absl::StatusOr<CompiledExpression> Create(const BuilderInstanceSharedConstPtr& builder,
                                                   const cel::expr::Expr& expr);

  // Creates an interpretable expression from xDS CEL expr format, making a copy of it.
  static absl::StatusOr<CompiledExpression> Create(const BuilderInstanceSharedConstPtr& builder,
                                                   const xds::type::v3::CelExpression& expr);

  // DEPRECATED. Use the above.
  static absl::StatusOr<CompiledExpression> Create(const BuilderInstanceSharedConstPtr& builder,
                                                   const google::api::expr::v1alpha1::Expr& expr);

  // Evaluates an expression for a request. The arena is used to hold intermediate computational
  // results and potentially the final value.
  absl::optional<CelValue>
  evaluate(Protobuf::Arena& arena, const ::Envoy::LocalInfo::LocalInfo* local_info,
           const StreamInfo::StreamInfo& info,
           const ::Envoy::Http::RequestHeaderMap* request_headers,
           const ::Envoy::Http::ResponseHeaderMap* response_headers,
           const ::Envoy::Http::ResponseTrailerMap* response_trailers) const;

  absl::StatusOr<CelValue> evaluate(const Activation& activation, Protobuf::Arena* arena) const;

  // Evaluates an expression and returns true if the expression evaluates to "true".
  // Returns false if the expression fails to evaluate.
  bool matches(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& headers) const;

private:
  explicit CompiledExpression(const BuilderInstanceSharedConstPtr& builder,
                              const cel::expr::Expr& expr)
      : builder_(builder), source_expr_(expr) {}
  const BuilderInstanceSharedConstPtr builder_;
  const cel::expr::Expr source_expr_;
  ExpressionPtr expr_;
};

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
