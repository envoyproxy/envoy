#include "source/extensions/filters/common/expr/custom_cel/example/example_custom_cel_vocabulary.h"

#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

#include "eval/public/activation.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_function_adapter.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

using google::api::expr::runtime::FunctionAdapter;

void throwException(absl::string_view function_name, absl::Status status);
void addLazyFunctionToActivation(Activation* activation, absl::string_view function_name,
                                 std::unique_ptr<CelFunction> function);
void addLazyFunctionToRegistry(CelFunctionRegistry* registry, absl::string_view function_name,
                               CelFunctionDescriptor descriptor);

void ExampleCustomCelVocabulary::fillActivation(Activation* activation, Protobuf::Arena& arena,
                                                const StreamInfo::StreamInfo& info,
                                                const Http::RequestHeaderMap* request_headers,
                                                const Http::ResponseHeaderMap* response_headers,
                                                const Http::ResponseTrailerMap* response_trailers) {
  setRequestHeaders(request_headers);
  setResponseHeaders(response_headers);
  setResponseTrailers(response_trailers);
  // variables
  activation->InsertValueProducer(
      CustomCelVariablesSetName,
      std::make_unique<CustomCelVariablesWrapper>(arena, info, request_headers, response_headers,
                                                  response_trailers));
  // Lazily evaluated functions only
  addLazyFunctionToActivation(activation, LazyEvalFuncNameGetDouble,
                              std::make_unique<GetDoubleCelFunction>(LazyEvalFuncNameGetDouble));
  addLazyFunctionToActivation(activation, LazyEvalFuncNameGetProduct,
                              std::make_unique<GetDoubleCelFunction>(LazyEvalFuncNameGetProduct));
  addLazyFunctionToActivation(activation, LazyEvalFuncNameGet99,
                              std::make_unique<GetDoubleCelFunction>(LazyEvalFuncNameGet99));
}

void addLazyFunctionToActivation(Activation* activation, absl::string_view function_name,
                                 std::unique_ptr<CelFunction> function) {
  absl::Status status = activation->InsertFunction(std::move(function));
  if (!status.ok()) {
    throwException(function_name, status);
  }
}

void addLazyFunctionToRegistry(CelFunctionRegistry* registry, absl::string_view function_name,
                               CelFunctionDescriptor descriptor) {
  absl::Status status = registry->RegisterLazyFunction(descriptor);
  if (!status.ok()) {
    throwException(function_name, status);
  }
}

void ExampleCustomCelVocabulary::registerFunctions(CelFunctionRegistry* registry) const {
  // lazily evaluated functions
  addLazyFunctionToRegistry(registry, LazyEvalFuncNameGetDouble,
                            GetDoubleCelFunction::createDescriptor(LazyEvalFuncNameGetDouble));
  addLazyFunctionToRegistry(registry, LazyEvalFuncNameGetProduct,
                            GetProductCelFunction::createDescriptor(LazyEvalFuncNameGetProduct));
  addLazyFunctionToRegistry(registry, LazyEvalFuncNameGet99,
                            Get99CelFunction::createDescriptor(LazyEvalFuncNameGet99));

  // eagerly evaluated functions
  absl::Status status = FunctionAdapter<CelValue, int64_t>::CreateAndRegister(
      EagerEvalFuncNameGetNextInt, false, getNextInt, registry);
  if (!status.ok()) {
    throwException(EagerEvalFuncNameGetNextInt, status);
  }
  status = FunctionAdapter<CelValue, int64_t>::CreateAndRegister(EagerEvalFuncNameGetSquareOf, true,
                                                                 getSquareOf, registry);
  if (!status.ok()) {
    throwException(EagerEvalFuncNameGetSquareOf, status);
  }
}

CustomCelVocabularyPtr ExampleCustomCelVocabularyFactory::createCustomCelVocabulary(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  // calling downcastAndValidate but not using the results
  // an exception will be thrown if the config is not validated
  MessageUtil::downcastAndValidate<const ExampleCustomCelVocabularyConfig&>(config,
                                                                            validation_visitor);
  return std::make_unique<ExampleCustomCelVocabulary>();
}

void throwException(absl::string_view function_name, absl::Status status) {
  throw EnvoyException(
      fmt::format("failed to register function '{}': {}", function_name, status.message()));
}

REGISTER_FACTORY(ExampleCustomCelVocabularyFactory, CustomCelVocabularyFactory);

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
