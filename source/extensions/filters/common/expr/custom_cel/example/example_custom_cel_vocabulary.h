#pragma once

#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel//custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"

#include "absl/strings/string_view.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function_registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

// Implementation of the CustomCelVocabulary interface.

using envoy::extensions::expr::custom_cel_vocabulary::example::v3::ExampleCustomCelVocabularyConfig;
using google::api::expr::runtime::Activation;
using google::api::expr::runtime::CelFunctionRegistry;

constexpr absl::string_view CustomVariablesName = "custom";
constexpr absl::string_view SourceVariablesName = "source";
constexpr absl::string_view LazyEvalFuncNameGetDouble = "GetDouble";
constexpr absl::string_view LazyEvalFuncNameGetProduct = "GetProduct";
constexpr absl::string_view LazyEvalFuncNameGet99 = "Get99";
constexpr absl::string_view EagerEvalFuncNameGetNextInt = "getNextInt";
constexpr absl::string_view EagerEvalFuncNameGetSquareOf = "getSquareOf";

class ExampleCustomCelVocabulary : public CustomCelVocabulary {
public:
  ExampleCustomCelVocabulary() = default;

  void fillActivation(Activation* activation, Protobuf::Arena& arena,
                      const StreamInfo::StreamInfo& info,
                      const Http::RequestHeaderMap* request_headers,
                      const Http::ResponseHeaderMap* response_headers,
                      const Http::ResponseTrailerMap* response_trailers) override;

  void registerFunctions(CelFunctionRegistry* registry) override;

  ~ExampleCustomCelVocabulary() override = default;
};

class ExampleCustomCelVocabularyFactory : public CustomCelVocabularyFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ExampleCustomCelVocabularyConfig>();
  }

  CustomCelVocabularyPtr
  createCustomCelVocabulary(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return "envoy.expr.custom_cel_vocabulary.example"; }
};

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
