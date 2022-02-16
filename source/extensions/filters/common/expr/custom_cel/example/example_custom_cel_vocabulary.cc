#include "source/extensions/filters/common/expr/custom_cel/example/example_custom_cel_vocabulary.h"

#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

#include "eval/public/activation.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"

// #pragma GCC diagnostic ignored "-Wunused-parameter"
// for file "eval/public/cel_function_adapter.h"
// This pragma directive is a temporary solution for the following problem:
// The GitHub pipeline uses a gcc compiler which generates an error about unused parameters
// for FunctionAdapter in cel_function_adapter.h
// The problem of the unused parameters has been fixed in more recent version of the cel-cpp
// library. However, it is not possible to upgrade the cel-cpp in envoy currently
// as it is waiting on the release of the one of its dependencies.
// TODO(b/219971889): Remove #pragma directives once the cel-cpp dependency has been upgraded.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "eval/public/cel_function_adapter.h"
#pragma GCC diagnostic pop

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

using google::api::expr::runtime::FunctionAdapter;

// Below are helper functions for adding a function mapping to an activation
// or registering a function in the CEL function registry.
//
// Any standard functions have to be converted to CelFunctions which can be done
// using the FunctionAdapter::Create or FunctionAdapter::CreateAndRegister methods.
//
// CelFunction::Evaluate takes Arena as a parameter.
// All standard functions will need to take Arena as a parameter.
//
// Activation: When adding a mapping to an activation for a variable/value producer or function,
// any previously added mappings with the same name are removed first.
//
// CEL Function Registry: When registering a function in the function registry,
// previous registrations with the same function descriptor cannot be removed or overridden.
// A message will be printed to the log.

// addValueProducerToActivation:
// Removes any envoy native value producer mapping of the same name from the activation.
// Replaces it with custom version.
template <typename T>
void addValueProducerToActivation(Activation* activation,
                                  const absl::string_view value_producer_name,
                                  std::unique_ptr<T> value_producer) {
  activation->RemoveValueEntry(value_producer_name);
  activation->InsertValueProducer(value_producer_name, std::move(value_producer));
}

// addLazyFunctionToActivation:
// Removes any envoy native version of a function with the same function descriptor from the
// activation. Adds the custom version to the activation.
void addLazyFunctionToActivation(Activation* activation, std::unique_ptr<CelFunction> function) {
  activation->RemoveFunctionEntries(function->descriptor());
  absl::Status status = activation->InsertFunction(std::move(function));
}

// addLazyFunctionToActivation:
// Converts standard function to CelFunction using FunctionAdapter::Create
// Removes any envoy native version of a function with the same function descriptor from the
// activation. Adds the custom version to the activation.
template <typename ReturnType, typename... Arguments>
void addLazyFunctionToActivation(
    Activation* activation, absl::string_view function_name, bool receiver_type,
    std::function<ReturnType(Protobuf::Arena*, Arguments...)> function) {
  auto result_or =
      FunctionAdapter<ReturnType, Arguments...>::Create(function_name, receiver_type, function);
  if (result_or.ok()) {
    auto cel_function = std::move(result_or.value());
    activation->RemoveFunctionEntries(cel_function->descriptor());
    absl::Status status = activation->InsertFunction(std::move(cel_function));
  }
}

// addLazyFunctionToRegistry:
// Previous function registrations with the same function descriptor cannot be removed
// from the registry.
// If there is an existing registration with the same name, the registration will not be
// overwritten. A message will be printed to the log.
void addLazyFunctionToRegistry(CelFunctionRegistry* registry, CelFunctionDescriptor descriptor) {
  absl::Status status = registry->RegisterLazyFunction(descriptor);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to register lazy function {}  in CEL function registry: {}",
                   descriptor.name(), status.message());
  }
}

// addLazyFunctionToRegistry:
// Converts a standard function to a CelFunction using FunctionAdapter::Create.
// Previous function registrations with the same function descriptor cannot be removed
// from the registry.
// If there is an existing registration with the same name, the registration will not be
// overwritten. A message will be printed to the log.
template <typename ReturnType, typename... Arguments>
void addLazyFunctionToRegistry(CelFunctionRegistry* registry, absl::string_view function_name,
                               bool receiver_type,
                               std::function<ReturnType(Protobuf::Arena*, Arguments...)> function) {
  auto result_or =
      FunctionAdapter<ReturnType, Arguments...>::Create(function_name, receiver_type, function);
  if (result_or.ok()) {
    auto cel_function = std::move(result_or.value());
    absl::Status status = registry->RegisterLazyFunction(cel_function->descriptor());
    if (!status.ok()) {
      ENVOY_LOG_MISC(debug, "Failed to register static function {}  in CEL function registry: {}",
                     function_name, status.message());
    }
  }
}

// addStaticFunctionToRegistry:
// Converts a standard function to a CelFunction using FunctionAdapter::CreateAndRegister.
// Previous function registrations with the same function descriptor cannot be removed
// from the registry.
// If there is an existing registration with the same name, the registration will not be
// overwritten. A message will be printed to the log.
template <typename ReturnType, typename... Arguments>
void addStaticFunctionToRegistry(
    CelFunctionRegistry* registry, absl::string_view function_name, bool receiver_type,
    std::function<ReturnType(Protobuf::Arena*, Arguments...)> function) {
  absl::Status status = FunctionAdapter<ReturnType, Arguments...>::CreateAndRegister(
      function_name, receiver_type, function, registry);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to register static function {}  in CEL function registry: {}",
                   function_name, status.message());
  }
}

void addStaticFunctionToRegistry(CelFunctionRegistry* registry,
                                 std::unique_ptr<CelFunction> function) {
  auto function_name = function->descriptor().name();
  absl::Status status = registry->Register(std::move(function));
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to register static function {}  in CEL function registry: {}",
                   function_name, status.message());
  }
}

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
                               std::make_unique<ExtendedRequestWrapper>(
                                   arena, request_headers, info, return_url_query_string_as_map_));
  // lazy functions only
  addLazyFunctionToActivation(activation, std::make_unique<GetDouble>(LazyFuncNameGetDouble));
  addLazyFunctionToActivation(activation, std::make_unique<GetProduct>(LazyFuncNameGetProduct));
  addLazyFunctionToActivation(activation, LazyFuncNameGetNextInt, false,
                              std::function<CelValue(Protobuf::Arena*, int64_t)>(getNextInt));
}

void ExampleCustomCELVocabulary::registerFunctions(CelFunctionRegistry* registry) {
  absl::Status status;

  // lazy functions
  addLazyFunctionToRegistry(registry, GetDouble::createDescriptor(LazyFuncNameGetDouble));
  addLazyFunctionToRegistry(registry, GetProduct::createDescriptor(LazyFuncNameGetProduct));
  addLazyFunctionToRegistry(registry, LazyFuncNameGetNextInt, false,
                            std::function<CelValue(Protobuf::Arena*, int64_t)>(getNextInt));

  // static functions
  addStaticFunctionToRegistry(registry, std::make_unique<Get99>(StaticFuncNameGet99));
  addStaticFunctionToRegistry(registry, StaticFuncNameGetSquareOf, true,
                              std::function<CelValue(Protobuf::Arena*, int64_t)>(getSquareOf));
}

CustomCELVocabularyPtr ExampleCustomCELVocabularyFactory::createCustomCELVocabulary(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  ExampleCustomCELVocabularyConfig custom_cel_config =
      MessageUtil::downcastAndValidate<const ExampleCustomCELVocabularyConfig&>(config,
                                                                                validation_visitor);
  return std::make_unique<ExampleCustomCELVocabulary>(
      custom_cel_config.return_url_query_string_as_map());
}

REGISTER_FACTORY(ExampleCustomCELVocabularyFactory, CustomCELVocabularyFactory);

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
