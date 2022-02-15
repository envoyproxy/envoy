#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"

#include "absl/strings/string_view.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function_registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {

using google::api::expr::runtime::Activation;
using google::api::expr::runtime::CelFunctionRegistry;

class CustomCELVocabulary {
public:
  CustomCELVocabulary() = default;

  // fillActivation - Adds variables/value producers and lazy functions to the current request's
  // activation, an activation being a mapping of names to their reference implementations.
  // Lazily evaluated functions require a two part registration:
  // (1) fillActivation will add the name of the function to the activation, and
  // (2) registerFunctions will add it to the CEL function registry.
  // The function mapping for a lazy function which is added to the activation in fillActivation
  // must be compatible with the function descriptor added to the registry in registerFunctions.
  // Static (eagerly evaluated) functions do not need to be registered with the activation,
  // only with the CEL function registry.
  // There are two sets of vocabulary (variables and functions):
  // (1) the envoy native CEL vocabulary (Request, Response, Connection, etc.)
  // which is added to the activation in the evaluator's createActivation function.
  // (2) the custom CEL vocabulary, which is added to the activation in fillActivation.
  // The envoy native vocabulary is registered first and the custom CEL vocabulary second.
  // In the event of overlap in the names of the vocabulary (e.g. "request", "response", etc.),
  // the fillActivation implementer can choose to either remove the envoy native mapping and
  // register a custom version in its place or to leave the envoy native mapping as is.
  // It is possible to remove lazy function entries from an activation,
  // which exists for the life of a request.
  // It is not possible to remove static function entries from the CEL function registry,
  // which exists for the life of the application.
  virtual void fillActivation(Activation* activation, Protobuf::Arena& arena,
                              const StreamInfo::StreamInfo& info,
                              const Http::RequestHeaderMap* request_headers,
                              const Http::ResponseHeaderMap* response_headers,
                              const Http::ResponseTrailerMap* response_trailers) PURE;

  // registerFunctions:
  // Registers both lazily evaluated and static (eagerly evaluated) functions
  // in the CEL function registry.
  // The registry, with its functions, is used by the CEL expression builder to
  // build CEL expressions.
  // There are two groups of functions.
  // (1) Native CEL built-in functions. These are functions like "+", "-" "*", "!".
  // (2) Custom CEL functions.
  // The Native CEL build-in functions are registered using the evaluator's
  // createBuilder function.
  // The custom CEL functions are registered here using registerFunctions.
  // Custom CEL functions may be (1) static (eagerly evaluated) or (2) lazy.
  // Lazy functions must also be added to the activation using fillActivation.
  // Static functions do not.
  // The lazy function activation mapping must match its counterpart lazy function
  // descriptor in the registry.
  // Once registered, the function registration cannot be removed from the registry
  // or overridden, as it can for an activation mappings.
  virtual void registerFunctions(CelFunctionRegistry* registry) PURE;

  virtual ~CustomCELVocabulary() = default;

protected:
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

using CustomCELVocabularyPtr = std::unique_ptr<CustomCELVocabulary>;

// CustomCELVocabularyFactory
// Creates a CustomCELVocabulary implementation instance.
class CustomCELVocabularyFactory : public Envoy::Config::TypedFactory {
public:
  std::string category() const override { return "envoy.expr.custom_cel_vocabulary_config"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  virtual CustomCELVocabularyPtr
  createCustomCELVocabulary(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
