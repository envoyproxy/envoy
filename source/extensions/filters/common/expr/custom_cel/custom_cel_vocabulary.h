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

  // fillActivation - adds variables/value producers to the activation,
  // an activation being a mapping of names to their reference implementations.
  // Lazily evaluated functions require a two part registration:
  // (1) fillActivation will add the name of the function to the activation, and
  // (2) registerFunctions will add it to the CEL function registry.
  // Eagerly evaluated functions do not need to be registered with the activation,
  // only with the CEL function registry.
  // There are two sets of vocabulary (variables and functions):
  // (1) the envoy native CEL vocabulary (Request, Response, Connection, etc.)
  // // which is added to the activation in the evaluator's createActivation
  // (2) the custom CEL vocabulary, which is added to the activation in fillActivation.
  // The envoy native vocabulary is registered first and the custom CEL vocabulary second.
  // In the event of overlap in the names of the vocabulary (e.g. "request", "response", etc.),
  // the fillActivation implementer can choose to remove the envoy native registration and
  // register a custom version in its place.
  // It is not possible to remove function entries from the registry which exists for the life of
  // the application. But it is possible to remove them from the activation for the life of the
  // request.
  virtual void fillActivation(Activation* activation, Protobuf::Arena& arena,
                              const StreamInfo::StreamInfo& info,
                              const Http::RequestHeaderMap* request_headers,
                              const Http::ResponseHeaderMap* response_headers,
                              const Http::ResponseTrailerMap* response_trailers) PURE;

  // registerFunctions: registers both lazily evaluated and eagerly evaluated functions
  // in the CEL function registry.
  virtual void registerFunctions(CelFunctionRegistry* registry) PURE;

  virtual ~CustomCELVocabulary() = default;

protected:
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

using CustomCELVocabularyPtr = std::unique_ptr<CustomCELVocabulary>;

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
