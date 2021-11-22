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
                                   const Http::ResponseTrailerMap* response_trailers) const {

  // words
  activation->InsertValueProducer("custom", std::make_unique<CustomVocabularyWrapper>(arena, info));
  // functions
  auto func_name = absl::string_view("LazyConstFuncReturns99");
  absl::Status status = activation->InsertFunction(std::make_unique<ConstCelFunction>(func_name));
}

void CustomLibrary::RegisterFunctions(CelFunctionRegistry* registry) const {
  // lazy functions
  auto status =
      registry->RegisterLazyFunction(ConstCelFunction::CreateDescriptor("LazyConstFuncReturns99"));
  if (!status.ok()) {
    throw EnvoyException(absl::StrCat("failed to register lazy functions: ", status.message()));
  }

  // eagerly evaluated functions
  status = google::api::expr::runtime::FunctionAdapter<CelValue, int64_t>::CreateAndRegister(
      "EagerConstFuncReturns99", false,
      [](Protobuf::Arena* arena, int64_t i) -> CelValue { return GetConstValue(arena, i); },
      registry);
  if (!status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register eagerly evaluated functions: ", status.message()));
  }
}

CustomLibraryPtr
CustomLibraryFactory::createInterface(const Protobuf::Message& config,
                                      ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& custom_library_config_typed_config =
      MessageUtil::downcastAndValidate<const envoy::config::core::v3::TypedExtensionConfig&>(
          config, validation_visitor);
  const auto custom_library_config = MessageUtil::anyConvertAndValidate<CustomLibraryConfig>(
      custom_library_config_typed_config.typed_config(), validation_visitor);
  auto library = std::make_unique<CustomLibrary>();
  library->set_replace_default_library(
      custom_library_config.replace_default_library_in_case_of_overlap());
  return library;
}

REGISTER_FACTORY(CustomLibraryFactory, BaseCustomLibraryFactory);

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
