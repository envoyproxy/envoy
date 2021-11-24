#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/extensions/rbac/custom_library_config/v3/custom_library.pb.h"
#include "envoy/extensions/rbac/custom_library_config/v3/custom_library.pb.validate.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/library/custom_functions.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "eval/public/activation.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_function_adapter.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

using Activation = google::api::expr::runtime::Activation;
using CelFunctionRegistry = google::api::expr::runtime::CelFunctionRegistry;
using CustomLibraryConfig = envoy::extensions::rbac::custom_library_config::v3::CustomLibraryConfig;

constexpr absl::string_view CustomVocabularyName = "custom";
constexpr absl::string_view LazyEvalFuncGetDoubleName = "GetDouble";
constexpr absl::string_view EagerEvalFuncGetNextIntName = "GetNextInt";

class CustomLibrary {
public:
  CustomLibrary(const bool replace_default_library)
      : replace_default_library_(replace_default_library) {}
  void FillActivation(Activation* activation,
                      Protobuf::Arena& arena,
                      const StreamInfo::StreamInfo& info,
                      const Http::RequestHeaderMap* request_headers,
                      const Http::ResponseHeaderMap* response_headers,
                      const Http::ResponseTrailerMap* response_trailers);

  void RegisterFunctions(CelFunctionRegistry* registry) const;

  bool replace_default_library() const { return replace_default_library_; }

private:
  const bool replace_default_library_;
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

using CustomLibraryPtr = std::unique_ptr<CustomLibrary>;

class BaseCustomLibraryFactory : public Envoy::Config::TypedFactory {
public:
  std::string category() const override { return "envoy.rbac.custom_library_config"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new CustomLibraryConfig()};
  }
  virtual CustomLibraryPtr createLibrary(const Protobuf::Message& config,
                                         ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

class CustomLibraryFactory : public BaseCustomLibraryFactory {
public:
  CustomLibraryPtr createLibrary(const Protobuf::Message& config,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return "envoy.rbac.custom_library_config.custom_library"; }
};

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
