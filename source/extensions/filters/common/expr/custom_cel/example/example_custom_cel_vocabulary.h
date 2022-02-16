#pragma once

#include "envoy/extensions/expr/custom_cel_vocabulary/example/v3/config.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"

#include "absl/strings/string_view.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function_registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

// Implementation of the CustomCELVocabulary interface.

using envoy::extensions::expr::custom_cel_vocabulary::example::v3::ExampleCustomCELVocabularyConfig;
using google::api::expr::runtime::Activation;
using google::api::expr::runtime::CelFunctionRegistry;

// variable set / activation value producer names
constexpr absl::string_view CustomVariablesName = "custom";
constexpr absl::string_view SourceVariablesName = "source";
constexpr absl::string_view ExtendedRequestVariablesName = "request";

// function names
constexpr absl::string_view LazyFuncNameGetDouble = "GetDouble";
constexpr absl::string_view LazyFuncNameGetProduct = "GetProduct";
constexpr absl::string_view LazyFuncNameGet99 = "Get99";
constexpr absl::string_view StaticFuncNameGetNextInt = "getNextInt";
constexpr absl::string_view StaticFuncNameGetSquareOf = "getSquareOf";

class ExampleCustomCELVocabulary : public CustomCELVocabulary {
public:
  ExampleCustomCELVocabulary(bool return_url_query_string_as_map)
      : return_url_query_string_as_map_(return_url_query_string_as_map) {}

  void fillActivation(Activation* activation, Protobuf::Arena& arena,
                      const StreamInfo::StreamInfo& info,
                      const Http::RequestHeaderMap* request_headers,
                      const Http::ResponseHeaderMap* response_headers,
                      const Http::ResponseTrailerMap* response_trailers) override;

  void registerFunctions(CelFunctionRegistry* registry) override;

  // return_url_query_string_as_map: url query string be returned as string or map.
  // This is user provided and comes from the ExampleCustomCELVocabularyConfig proto.
  bool returnUrlQueryStringAsMap() { return return_url_query_string_as_map_; }

  ~ExampleCustomCELVocabulary() override = default;

private:
  bool return_url_query_string_as_map_;
};

class ExampleCustomCELVocabularyFactory : public CustomCELVocabularyFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ExampleCustomCELVocabularyConfig>();
  }

  CustomCELVocabularyPtr
  createCustomCELVocabulary(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return "envoy.expr.custom_cel_vocabulary.example"; }
};

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
