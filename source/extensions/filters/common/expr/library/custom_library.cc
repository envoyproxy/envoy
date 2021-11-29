#include "source/extensions/filters/common/expr/library/custom_library.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/extensions/rbac/custom_library_config/v3/custom_library.pb.h"
#include "envoy/extensions/rbac/custom_library_config/v3/custom_library.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/library/custom_vocabulary.h"

#include "eval/public/cel_function_adapter.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

void ThrowException(absl::string_view function_name, absl::Status status);

void CustomLibrary::FillActivation(Activation* activation, Protobuf::Arena& arena,
                                   const StreamInfo::StreamInfo& info,
                                   const Http::RequestHeaderMap* request_headers,
                                   const Http::ResponseHeaderMap* response_headers,
                                   const Http::ResponseTrailerMap* response_trailers) {
  request_headers_ = request_headers;
  response_headers_ = response_headers;
  response_trailers_ = response_trailers;
  // vocabulary
  activation->InsertValueProducer(
      CustomVocabularyName, std::make_unique<CustomVocabularyWrapper>(
                                arena, info, request_headers, response_headers, response_trailers));
  // Lazily evaluated functions only
  absl::Status status;
  status =
      activation->InsertFunction(std::make_unique<GetDoubleCelFunction>(LazyEvalFuncNameGetDouble));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetDouble, status);
  }
  status = activation->InsertFunction(
      std::make_unique<GetProductCelFunction>(LazyEvalFuncNameGetProduct));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetProduct, status);
  }
  status = activation->InsertFunction(std::make_unique<Get99CelFunction>(LazyEvalFuncNameGet99));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGet99, status);
  }
}

void CustomLibrary::RegisterFunctions(CelFunctionRegistry* registry) const {
  absl::Status status;
  // lazily evaluated functions
  status = registry->RegisterLazyFunction(
      GetDoubleCelFunction::CreateDescriptor(LazyEvalFuncNameGetDouble));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetDouble, status);
  }
  status = registry->RegisterLazyFunction(
      GetProductCelFunction::CreateDescriptor(LazyEvalFuncNameGetProduct));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGetProduct, status);
  }
  status =
      registry->RegisterLazyFunction(Get99CelFunction::CreateDescriptor(LazyEvalFuncNameGet99));
  if (!status.ok()) {
    ThrowException(LazyEvalFuncNameGet99, status);
  }

  // eagerly evaluated functions
  status = google::api::expr::runtime::FunctionAdapter<CelValue, int64_t>::CreateAndRegister(
      EagerEvalFuncNameGetNextInt, false, GetNextInt, registry);
  if (!status.ok()) {
    ThrowException(EagerEvalFuncNameGetNextInt, status);
  }
  status = google::api::expr::runtime::FunctionAdapter<CelValue, int64_t>::CreateAndRegister(
      EagerEvalFuncNameGetSquareOf, true, GetSquareOf, registry);
  if (!status.ok()) {
    ThrowException(EagerEvalFuncNameGetSquareOf, status);
  }
}

CustomLibraryPtr
CustomLibraryFactory::createLibrary(const Protobuf::Message& config,
                                    ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const envoy::config::core::v3::TypedExtensionConfig&>(
          config, validation_visitor);
  const auto custom_library_config = MessageUtil::anyConvertAndValidate<CustomLibraryConfig>(
      typed_config.typed_config(), validation_visitor);
  auto custom_library = std::make_unique<CustomLibrary>(
      custom_library_config.replace_default_library_in_case_of_overlap());
  return custom_library;
}

void ThrowException(absl::string_view function_name, absl::Status status) {
  throw EnvoyException(
      fmt::format("failed to register function '{}': {}", function_name, status.message()));
}

REGISTER_FACTORY(CustomLibraryFactory, BaseCustomLibraryFactory);

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
