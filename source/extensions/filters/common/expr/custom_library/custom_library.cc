#include "source/extensions/filters/common/expr/custom_library/custom_library.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomLibrary {

absl::optional<CelValue> CustomVocabularyWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == "team") {
    return CelValue::CreateStringView("swg");
  } else if (value == "ip") {
    std::cout << "custom vocabulary ip!" << std::endl;
    auto upstream_local_address = info_.upstreamLocalAddress();
    if (upstream_local_address != nullptr) {
      return CelValue::CreateStringView(upstream_local_address->asStringView());
    } else {
      std::cout << "upstream_local_address is null" << std::endl;
    }
  }

  return {};
}

void CustomLibrary::FillActivation(Activation *activation, Protobuf::Arena& arena,
                                  const StreamInfo::StreamInfo& info) const {

  //words
  activation->InsertValueProducer("custom",
                                      std::make_unique<CustomVocabularyWrapper>(arena, info));
  //functions
  auto func_name = absl::string_view("LazyConstFuncReturns99");
  absl::Status status = activation->InsertFunction(std::make_unique<ConstCelFunction>(func_name));
}

void CustomLibrary::RegisterFunctions(CelFunctionRegistry* registry) const {
  //lazy functions
  auto status = registry->RegisterLazyFunction(ConstCelFunction::CreateDescriptor("LazyConstFuncReturns99"));
  if (!status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register lazy functions: ", status.message()));
  }
  //eagerly evaluated functions
  status = google::api::expr::runtime::FunctionAdapter<CelValue, int64_t>::
  CreateAndRegister(
      "EagerConstFuncReturns99", false,
      [](Protobuf::Arena* arena, int64_t i)
          -> CelValue { return GetConstValue(arena, i); },
      registry);
  if (!status.ok()) {
    throw EnvoyException(
        absl::StrCat("failed to register eagerly evaluated functions: ", status.message()));
  }
}

CustomLibraryPtr CustomLibraryFactory::createInterface(
      const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const CustomLibraryConfig&>(config, validation_visitor);
  const auto config = MessageUtil::anyConvertAndValidate<
      CustomLibraryConfig>(
      typed_config.config().typed_config(), validation_visitor);
  bool replace_default_vocab = config.replace_default_vocab_in_case_of_overlap();
  if (replace_default_vocab) {
    std::cout << "********************* replace_default_vocab is true" << std::endl;
  } else {
    std::cout << "********************* replace_default_vocab is false" << std::endl;
  }
  return std::make_unique<CustomLibrary>();
}

REGISTER_FACTORY(CustomLibraryFactory, BaseCustomLibraryFactory);

} // namespace CustomLibrary
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy