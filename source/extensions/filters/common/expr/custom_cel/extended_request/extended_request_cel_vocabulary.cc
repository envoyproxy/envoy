#include "source/extensions/filters/common/expr/custom_cel/extended_request/extended_request_cel_vocabulary.h"

#include "envoy/extensions/expr/custom_cel_vocabulary/extended_request/v3/config.pb.h"
#include "envoy/extensions/expr/custom_cel_vocabulary/extended_request/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_attributes.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_functions.h"

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
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "eval/public/cel_function_adapter.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

using envoy::extensions::expr::custom_cel_vocabulary::extended_request::v3::
    ExtendedRequestCelVocabularyConfig;
using google::api::expr::runtime::Activation;
using google::api::expr::runtime::CelFunction;
using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelFunctionRegistry;
using google::api::expr::runtime::FunctionAdapter;

// Below are several helper functions for adding a function mapping to an activation
// or registering a function in the CEL function registry.
//
// Any standard functions have to be converted to CelFunctions which can be done
// using the FunctionAdapter::Create or FunctionAdapter::CreateAndRegister methods.
//
// CelFunction::Evaluate takes Arena as a parameter.
// All standard functions will need to take Arena as a parameter.
// Any declared parameters must be of type CelValue.
// Non-CelValue parameters can be added via lambda captures.
//
// Activation: When adding a mapping to an activation for a variable/value producer or function,
// any previously added mappings with the same name are removed first.
//
// CEL Function Registry: When registering a function in the function registry,
// previous registrations with the same function descriptor cannot be removed or overridden.
// A message will be printed to the log.

template <typename T>
void addValueProducerToActivation(Activation* activation,
                                  const absl::string_view value_producer_name,
                                  std::unique_ptr<T> value_producer) {
  activation->RemoveValueEntry(value_producer_name);
  activation->InsertValueProducer(value_producer_name, std::move(value_producer));
}

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

void addLazyFunctionToRegistry(CelFunctionRegistry* registry, CelFunctionDescriptor descriptor) {
  absl::Status status = registry->RegisterLazyFunction(descriptor);
  if (!status.ok()) {
    ENVOY_LOG_MISC(debug, "Failed to register lazy function {}  in CEL function registry: {}",
                   descriptor.name(), status.message());
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

void ExtendedRequestCelVocabulary::fillActivation(
    Activation* activation, Protobuf::Arena& arena, const StreamInfo::StreamInfo& info,
    const Http::RequestHeaderMap* request_headers, const Http::ResponseHeaderMap* response_headers,
    const Http::ResponseTrailerMap* response_trailers) {
  request_headers_ = request_headers;
  response_headers_ = response_headers;
  response_trailers_ = response_trailers;

  // all of the following require request_headers
  if (request_headers) {
    // attributes
    addValueProducerToActivation(
        activation, ExtendedRequest,
        std::make_unique<ExtendedRequestWrapper>(arena, request_headers, info,
                                                 return_url_query_string_as_map_));

    // lazy functions only
    addLazyFunctionToActivation(
        activation, LazyFuncNameCookie, false,
        std::function<CelValue(Protobuf::Arena*)>(
            [request_headers](Protobuf::Arena* arena) { return cookie(arena, *request_headers); }));
    addLazyFunctionToActivation(activation, LazyFuncNameCookieValue, false,
                                std::function<CelValue(Protobuf::Arena*, CelValue)>(
                                    [request_headers](Protobuf::Arena* arena, CelValue key) {
                                      return cookieValue(arena, *request_headers, key);
                                    }));
  }
}

void ExtendedRequestCelVocabulary::registerFunctions(CelFunctionRegistry* registry) {
  absl::Status status;

  // lazy functions
  addLazyFunctionToRegistry(registry, CelFunctionDescriptor(LazyFuncNameCookie, false, {}));
  addLazyFunctionToRegistry(
      registry, CelFunctionDescriptor(LazyFuncNameCookieValue, false, {CelValue::Type::kString}));

  // static functions
  addStaticFunctionToRegistry(registry, std::make_unique<UrlFunction>(StaticFuncNameUrl));
}

CustomCelVocabularyPtr ExtendedRequestCelVocabularyFactory::createCustomCelVocabulary(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  ExtendedRequestCelVocabularyConfig custom_cel_config =
      MessageUtil::downcastAndValidate<const ExtendedRequestCelVocabularyConfig&>(
          config, validation_visitor);
  return std::make_unique<ExtendedRequestCelVocabulary>(
      custom_cel_config.return_url_query_string_as_map());
}

REGISTER_FACTORY(ExtendedRequestCelVocabularyFactory, CustomCelVocabularyFactory);

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
