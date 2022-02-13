#include "eval/public/activation.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_function_adapter.h"
#include "eval/public/cel_value.h"

#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"
#include "source/extensions/filters/common/expr/custom_cel/example/example_custom_cel_vocabulary.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

using google::api::expr::runtime::FunctionAdapter;

void addLazyFunctionToActivation(Activation* activation,
                                 std::unique_ptr<CelFunction> function, CelFunctionDescriptor descriptor);
template <typename T>
void addValueProducerToActivation(Activation* activation,
                                  const absl::string_view value_producer_name,
                                  std::unique_ptr<T> value_producer);

void ExampleCustomCELVocabulary::fillActivation(Activation* activation, Protobuf::Arena& arena,
                                                const StreamInfo::StreamInfo& info,
                                                const Http::RequestHeaderMap* request_headers,
                                                const Http::ResponseHeaderMap* response_headers,
                                                const Http::ResponseTrailerMap* response_trailers) {
  request_headers_ = request_headers;
  response_headers_ = response_headers;
  response_trailers_ = response_trailers;

  // variables
  addValueProducerToActivation(activation, CustomVariablesName,
                               std::make_unique<CustomWrapper>(arena, info));
  addValueProducerToActivation(activation, SourceVariablesName,
                               std::make_unique<SourceWrapper>(arena, info));
  addValueProducerToActivation(activation, ExtendedRequestVariablesName,
                               std::make_unique<ExtendedRequestWrapper>(arena, request_headers, info, return_url_query_string_as_map_));
  // Lazily evaluated functions only
  addLazyFunctionToActivation(activation,
                              std::make_unique<GetDoubleCELFunction>(LazyEvalFuncNameGetDouble),
                              GetDoubleCELFunction::createDescriptor(LazyEvalFuncNameGetDouble));
  addLazyFunctionToActivation(activation,
                              std::make_unique<GetProductCELFunction>(LazyEvalFuncNameGetProduct),
                              GetProductCELFunction::createDescriptor(LazyEvalFuncNameGetProduct));
  addLazyFunctionToActivation(activation,
                              std::make_unique<Get99CELFunction>(LazyEvalFuncNameGet99),
                              Get99CELFunction::createDescriptor(LazyEvalFuncNameGet99));
}

// addValueProducerToActivation: Removes any envoy native version of a value producer
// with the same name from the activation.
// Adds the custom version to the activation.
template <typename T>
void addValueProducerToActivation(Activation* activation,
                                  const absl::string_view value_producer_name,
                                  std::unique_ptr<T> value_producer) {
  activation->RemoveValueEntry(value_producer_name);
  activation->InsertValueProducer(value_producer_name, std::move(value_producer));
}

// addLazyFunctionToActivation: Removes any envoy native version of a function
// with the same function descriptor from the activation.
// Adds the custom version to the activation.
void addLazyFunctionToActivation(Activation* activation,
                                 std::unique_ptr<CelFunction> function, CelFunctionDescriptor descriptor) {
  activation->RemoveFunctionEntries(descriptor);
  absl::Status status = activation->InsertFunction(std::move(function));
}

void addLazyFunctionToRegistry(CelFunctionRegistry* registry, absl::string_view function_name, CelFunctionDescriptor descriptor) {
  absl::Status status = registry->RegisterLazyFunction(descriptor);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to register lazy function {}  in cel function registry: {}", function_name, status.message());
  }
}

template <typename ReturnType, typename... Arguments>
void addEagerFunctionToRegistry(absl::string_view function_name, bool receiver_type, std::function<ReturnType(Protobuf::Arena*, Arguments...)> function, CelFunctionRegistry* registry) {
   absl::Status status = FunctionAdapter<ReturnType, Arguments...>::CreateAndRegister(
          function_name, receiver_type, function, registry);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to register eager function {}  in cel function registry: {}", function_name, status.message());
  }
}

// registerFunctions:
// There is no way to remove previous entries from the registry.
// If there is an existing entry with the same name, the registration will not be overwritten.
// A message will be printed to the log.
void ExampleCustomCELVocabulary::registerFunctions(CelFunctionRegistry* registry) {
  absl::Status status;

  // lazily evaluated functions
  addLazyFunctionToRegistry(registry, LazyEvalFuncNameGetDouble, GetDoubleCELFunction::createDescriptor(LazyEvalFuncNameGetDouble));
  addLazyFunctionToRegistry(registry, LazyEvalFuncNameGetProduct, GetProductCELFunction::createDescriptor(LazyEvalFuncNameGetProduct));
  addLazyFunctionToRegistry(registry, LazyEvalFuncNameGet99, Get99CELFunction::createDescriptor(LazyEvalFuncNameGet99));

  addEagerFunctionToRegistry(EagerEvalFuncNameGetNextInt, false,
                             std::function<CelValue(Protobuf::Arena*, int64_t)>(getNextInt), registry);
  addEagerFunctionToRegistry(EagerEvalFuncNameGetSquareOf, true,
                             std::function<CelValue(Protobuf::Arena*, int64_t)>(getSquareOf), registry);
}

CustomCELVocabularyPtr ExampleCustomCELVocabularyFactory::createCustomCELVocabulary(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  ExampleCustomCELVocabularyConfig custom_cel_config =
      MessageUtil::downcastAndValidate<const ExampleCustomCELVocabularyConfig&>(config,validation_visitor);
  return std::make_unique<ExampleCustomCELVocabulary>(custom_cel_config.return_url_query_string_as_map());
}

REGISTER_FACTORY(ExampleCustomCELVocabularyFactory, CustomCELVocabularyFactory);

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
