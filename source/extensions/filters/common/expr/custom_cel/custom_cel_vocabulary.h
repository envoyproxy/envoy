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
namespace CustomCel {

using google::api::expr::runtime::Activation;
using google::api::expr::runtime::CelFunctionRegistry;

// CustomCelVocabulary
//
// Background Information:
//
// CelExpression: CelExpressions can contain values, variables and functions.
// The variables and functions can be either (1) stateless or (2) stateful.
// In CEL, stateless functions are called "static" and stateful functions are "lazy".
//
// CelExpressions are evaluated with the aid of certain data structures
// which contain references to variables and functions implementations.
// The data structures are (1) the CelFunctionRegistry and (2) the Activation.
//
// CelFunctionRegistry: A registry of all CEL functions which may be used in
// CelExpressions.
// The registry contains registrations for both stateless and stateful functions.
//
// Activation: contains a mapping of function names and variable names to their
// reference implementations.
// A new activation is created for every CelExpression and is used for the
// evaluation of the CelExpression.
// An activation contains variables and stateful, but not stateless functions.
//
// CelExpressionBuilder: A CelExpressionBuilder contains the registry of all allowable CEL
// functions. The CelExpressionBuilder is used to create CelExpressions.
// An activation is created for every CelExpression.

// Vocabulary: Variables and functions constitute the "vocabulary" used to
// evaluate expressions.
//
// There are multiple vocabularies in use:
//
// (1) the Built-in CEL Vocabulary
// https://github.com/google/cel-spec/blob/master/doc/langdef.md#list-of-standard-definitions
// Built-in CEL Functions include "+", "-", "*".
// These are stateless and are registered with the CEL function registry.
//
// (2) the Envoy defined Vocabulary,
// https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/advanced/attributes
// Envoy defined CEL attributes:  Envoy has defined certain stateful CEL attributes
// like Request, Response, Connection, etc.
// These are added to the activation.
//
// and (3) the Custom Cel Vocabulary
// These are custom definitions which a user can define.
// Custom CEL stateless functions have to be added to the registry.
// Custom CEL variables and stateful functions have to be added to the activation.
//
// This interface is intended to allow for the addition and use of a CustomCelVocabulary.
//
// A note on function definitions: either standard functions or CelFunctions can be used.
// The standard functions will be converted to CelFunctions when added to the
// registry and activation.
// All standard functions will need a Protobuf Arena because CelFunction::Evaluate takes
// Arena as a parameter.

class CustomCelVocabulary {
public:
  CustomCelVocabulary() = default;

  // fillActivation:
  //
  // Adds variables/value producers and stateful/lazy functions to the current request's
  // activation.
  // Stateful/lazy functions require a two part registration:
  // (1) fillActivation will add a function mapping to the activation, and
  // (2) registerFunctions will add it to the CEL function registry.
  // The function mapping for a stateful/lazy function which is added to the
  // activation in fillActivation must be compatible with the function descriptor
  // added to the registry in registerFunctions.
  // Stateless/static functions do not need to be registered with the activation,
  // only with the CEL function registry.
  //
  // The activation contains two sets of vocabulary (variables and functions):
  // (1) the Envoy defined CEL vocabulary (Request, Response, Connection, etc.)
  // which is added to the activation in the evaluator's createActivation function.
  // (2) the custom CEL vocabulary, which is added to the activation in fillActivation.
  // The Envoy defined vocabulary is registered first and the custom CEL vocabulary second.
  //
  // The implementer of this interface can choose to override the Envoy defined CEL vocabulary
  // (Request, Response, Connection, etc.) with a custom implementation.
  // The mappings for the Envoy defined CEL vocabulary would have to be removed from the
  // activation and replaced with the mappings for the custom CEL vocabulary.
  //
  // It is possible to remove entries from an activation.
  // It is not possible to remove entries from the CEL function registry.
  virtual void fillActivation(Activation* activation, Protobuf::Arena& arena,
                              const StreamInfo::StreamInfo& info,
                              const Http::RequestHeaderMap* request_headers,
                              const Http::ResponseHeaderMap* response_headers,
                              const Http::ResponseTrailerMap* response_trailers) PURE;

  // registerFunctions:
  //
  // Registers both stateful/lazy and stateless/static functions in the CEL function registry.
  // The registry, with its functions, is used by the CEL expression builder to
  // build CEL expressions.
  // The registry contains two groups of functions.
  // (1) Built-in CEL  functions. ("+", "-" "*", "!", etc.)
  // (2) Custom CEL functions.
  // (There are currently no Envoy defined functions which are registered).
  // The CelExpressionBuilder which is used to create CelExpressions contains a registry
  // in which the built-in CEL functions have been registered.
  // The custom CEL functions are registered in the registry using registerFunctions.
  // Stateful/lazy functions must also be added to the activation using fillActivation.
  // Stateless/static functions do not have to be added to the activation.
  // The stateful/lazy function activation mapping must match its counterpart
  // stateful/lazy function descriptor in the registry.
  // Once registered, the function registration cannot be removed from the registry
  // or overridden, as it can for an activation mapping.
  virtual void registerFunctions(CelFunctionRegistry* registry) PURE;

  virtual ~CustomCelVocabulary() = default;

protected:
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

using CustomCelVocabularyPtr = std::unique_ptr<CustomCelVocabulary>;

// CustomCelVocabularyFactory
// Creates a CustomCelVocabulary implementation instance.
class CustomCelVocabularyFactory : public Envoy::Config::TypedFactory {
public:
  std::string category() const override { return "envoy.expr.custom_cel_vocabulary_config"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  virtual CustomCelVocabularyPtr
  createCustomCelVocabulary(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
