#include "source/extensions/filters/common/expr/library/custom_library.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/common/expr/library/custom_vocabulary.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

void CustomLibrary::FillActivation(Activation* activation, Protobuf::Arena& arena,
                                   const StreamInfo::StreamInfo& info,
                                   const Http::RequestHeaderMap* request_headers,
                                   const Http::ResponseHeaderMap* response_headers,
                                   const Http::ResponseTrailerMap* response_trailers) {

  request_headers_ = request_headers;
  response_headers_ = response_headers;
  response_trailers_ = response_trailers;
  // words
  activation->InsertValueProducer(CustomVocabularyName,
                                  std::make_unique<CustomVocabularyWrapper>(arena,
                                                                            info,
                                                                            request_headers,
                                                                            response_headers,
                                                                            response_trailers));
  // functions
  absl::Status status = activation->InsertFunction(
      std::make_unique<GetDoubleCelFunction>(LazyEvalFuncGetDoubleName));
}

void CustomLibrary::RegisterFunctions(CelFunctionRegistry* registry) const {
  std::cout << std::endl << "*************** RegisterFunctions" << std::endl;
  absl::Status status;
  // lazily evaluated functions
  status =
      registry->RegisterLazyFunction(GetDoubleCelFunction::CreateDescriptor(LazyEvalFuncGetDoubleName));
  if (!status.ok()) {
    throw EnvoyException(absl::StrCat("failed to register lazy functions: ", status.message()));
  }

  // eagerly evaluated functions
  status = google::api::expr::runtime::FunctionAdapter<CelValue, int64_t>::CreateAndRegister(
      EagerEvalFuncGetNextIntName, false,
      [](Protobuf::Arena* arena, int64_t i) -> CelValue { return GetNextInt(arena, i); },
      registry);
  if (!status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register eagerly evaluated functions: ", status.message()));
  }
}

CustomLibraryPtr
CustomLibraryFactory::createLibrary(const Protobuf::Message& config,
                                    ProtobufMessage::ValidationVisitor& validation_visitor) {
  std::cout << "********** CustomLibraryFactory::createLibrary" << std::endl;
  const auto& typed_config =
      MessageUtil::downcastAndValidate<const envoy::config::core::v3::TypedExtensionConfig&>(
          config, validation_visitor);
  const auto custom_library_config = MessageUtil::anyConvertAndValidate<CustomLibraryConfig>(
      typed_config.typed_config(), validation_visitor);
  auto custom_library = std::make_unique<CustomLibrary>(
      custom_library_config.replace_default_library_in_case_of_overlap());
  return custom_library;
}

REGISTER_FACTORY(CustomLibraryFactory, BaseCustomLibraryFactory);

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
