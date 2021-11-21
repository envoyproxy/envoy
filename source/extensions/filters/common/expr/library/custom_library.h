#pragma once

#include "source/common/protobuf/protobuf.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/library/custom_functions.h"

#include "eval/public/activation.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "eval/public/cel_function_adapter.h"

#include "envoy/extensions/filters/common/expr/custom_library/v3/custom_library.pb.h"
#include "envoy/extensions/filters/common/expr/custom_library/v3/custom_library.pb.validate.h"
#include "envoy/protobuf/message_validator.h"

//for vocabulary, table
//value producer name, pointer to wrapper for that value
//for lazy functions, table
/*
 * function name, function class definition - need the name in two places
 * eagerly evaluated functions, just add to the list
 */
namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

using Activation = google::api::expr::runtime::Activation;
using CelFunctionRegistry = google::api::expr::runtime::CelFunctionRegistry;
using CustomLibraryConfig = envoy::extensions::filters::common::expr::custom_library::v3::CustomLibraryConfig;

class CustomVocabularyWrapper : public BaseWrapper {
 public:
  CustomVocabularyWrapper(Protobuf::Arena& arena,
                          const StreamInfo::StreamInfo& info)
      : arena_(arena), info_(info) {
    arena_.SpaceUsed();
    info_.attemptCount();
  }
  absl::optional<CelValue> operator[](CelValue key) const override;

 private:
  Protobuf::Arena& arena_;
  const StreamInfo::StreamInfo& info_;
};

class CustomLibrary {
 public:
  void FillActivation(Activation* activation, Protobuf::Arena& arena,
                      const StreamInfo::StreamInfo& info,
                      const Http::RequestHeaderMap* request_headers,
                      const Http::ResponseHeaderMap* response_headers,
                      const Http::ResponseTrailerMap* response_trailers) const;

  void RegisterFunctions(CelFunctionRegistry* registry) const;
  bool replace_default_library_;
};

using CustomLibraryPtr = std::unique_ptr<CustomLibrary>;

class BaseCustomLibraryFactory : public Envoy::Config::TypedFactory {
 public:
  std::string category() const override { return "envoy.rbac.custom_library_config"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new CustomLibraryConfig()};
  }
  virtual CustomLibraryPtr createInterface(
      const Protobuf::Message& config,
      ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

class CustomLibraryFactory : public BaseCustomLibraryFactory {
 public:
  CustomLibraryPtr createInterface(
      const Protobuf::Message& config,
      ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override {
    return "envoy.expr.custom_library";
  }
};

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
