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

// CustomCELVocabulary
//
// Background Definitions:
//
// CEL Expressions are evaluated with the aid of certain data structures
// which contain references to variables and functions implementations.
// The data structures are (1) the Activation and (2) the CelFunctionRegistry.
//
// Activation: contains a mapping of names (of variables and functions) to their
// reference implementations.
// A new activation is created for every request.
//
// CEL Function Registry: A registry for functions which lasts the life of the application.
//
// Vocabulary: The variables and functions constitute the "vocabulary" used to
// evaluate expressions.
//
// There are multiple "vocabularies" in use: (1) the built-in CEL Vocabulary,
// (2) Envoy defined Vocabulary, and (3) Custom CEL Vocabulary
// Built-in CEL Functions are functions like "+", "-", "*".
// These are static and registered with the CEL function registry.
// Envoy defined CEL Variables:  Envoy has defined certain CEL variables/value producers like
// Request, Response, Connection, etc.
// These have been added to the Activation.
// Custom CEL Vocabulary: These are vocabulary definitions which a user can provider.
// Custom CEL Variables must be added to the activation.
// Custom CEL Static Functions must be added to the registry (they do not change for the life of the
// application). Custom CEL Lazy functions must be added to both the registry and the activation.
// This interface is intended to allow for the addition of CustomCELVocabularies.
//
// Functions: Functions can be (1) lazy functions or (2) static functions
// (eagerly evaluated).
// Either standard functions or CelFunctions can be used.
// The standard functions will be converted to CelFunctions when added to the
// registry and activation.
// All standard functions will need a Protobuf Arena because CelFunction::Evaluate takes
// Arena as a parameter.

class CustomCELVocabulary {
public:
  CustomCELVocabulary() = default;

  // fillActivation:
  // Adds variables/value producers and lazy functions to the current request's
  // activation, an activation being a mapping of names to their reference implementations.
  // Lazily evaluated functions require a two part registration:
  // (1) fillActivation will add the name of the function to the activation, and
  // (2) registerFunctions will add it to the CEL function registry.
  // The function mapping for a lazy function which is added to the activation in fillActivation
  // must be compatible with the function descriptor added to the registry in registerFunctions.
  // Static (eagerly evaluated) functions do not need to be registered with the activation,
  // only with the CEL function registry.
  // The Activation contains two sets of vocabulary (variables and functions):
  // (1) the Envoy defined CEL vocabulary (Request, Response, Connection, etc.)
  // which is added to the activation in the evaluator's createActivation function.
  // (2) the custom CEL vocabulary, which is added to the activation in fillActivation.
  // The Envoy defined vocabulary is registered first and the custom CEL vocabulary second.
  // In the event of overlap in the names of the vocabulary (e.g. "request", "response", etc.),
  // the fillActivation implementer can choose to either remove the envoy native mapping and
  // register a custom version in its place or to leave the envoy native mapping as is.
  // It is possible to remove lazy function entries from an activation,
  // which exists for the life of a request.
  // It is not possible to remove either lazy or static function entries from the
  // CEL function registry, which exists for the life of the application.
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
  // The registry contains two groups of functions.
  // (1) Native CEL built-in functions. These are functions like "+", "-" "*", "!".
  // (2) Custom CEL functions.
  // The Native CEL build-in functions are registered using the evaluator's
  // createBuilder function.
  // The custom CEL functions are registered here using registerFunctions.
  // Custom CEL functions may be (1) lazy or (2) static (eagerly evaluated).
  // Lazy functions must also be added to the activation using fillActivation.
  // Static functions are not.
  // The lazy function activation mapping must match its counterpart lazy function
  // descriptor in the registry.
  // Once registered, the function registration cannot be removed from the registry
  // or overridden, as it can for an activation mapping.
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
