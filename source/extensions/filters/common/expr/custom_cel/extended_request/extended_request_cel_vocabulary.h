#pragma once

#include "envoy/extensions/expr/custom_cel_vocabulary/extended_request/v3/config.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"
#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/custom_cel_functions.h"

#include "absl/strings/string_view.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function_registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

// Implementation of the CustomCelVocabulary interface.

// activation value producer names
constexpr absl::string_view ExtendedRequest = Request;

// function names
constexpr absl::string_view LazyFuncNameCookie = "cookie";
constexpr absl::string_view LazyFuncNameCookieValue = "cookieValue";
constexpr absl::string_view StaticFuncNameUrl = "url";

class ExtendedRequestCelVocabulary : public CustomCelVocabulary {
public:
  ExtendedRequestCelVocabulary(bool return_url_query_string_as_map)
      : return_url_query_string_as_map_(return_url_query_string_as_map) {}

  void fillActivation(google::api::expr::runtime::Activation* activation, Protobuf::Arena& arena,
                      const StreamInfo::StreamInfo& info,
                      const Http::RequestHeaderMap* request_headers,
                      const Http::ResponseHeaderMap* response_headers,
                      const Http::ResponseTrailerMap* response_trailers) override;

  void registerFunctions(google::api::expr::runtime::CelFunctionRegistry* registry) override;

  // return_url_query_string_as_map: url query string be returned as string or map.
  // This is user provided and comes from the ExtendedRequestCelVocabularyConfig proto.
  bool returnUrlQueryStringAsMap() { return return_url_query_string_as_map_; }

  ~ExtendedRequestCelVocabulary() override = default;

private:
  bool return_url_query_string_as_map_;
};

class ExtendedRequestCelVocabularyFactory : public CustomCelVocabularyFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::expr::custom_cel_vocabulary::extended_request::v3::
                                ExtendedRequestCelVocabularyConfig>();
  }

  CustomCelVocabularyPtr
  createCustomCelVocabulary(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return "envoy.expr.custom_cel_vocabulary.extended_request"; }
};

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
