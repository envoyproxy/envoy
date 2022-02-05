#pragma once

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/context.h"

#include "absl/strings/string_view.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function_registry.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {

using google::api::expr::runtime::Activation;
using google::api::expr::runtime::CelFunctionRegistry;

class CustomCelVocabulary {
public:
  CustomCelVocabulary() {}

  // FillActivation can register variable sets using InsertValueProducer.
  // Lazy function
  virtual void fillActivation(Activation* activation, Protobuf::Arena& arena,
                              const StreamInfo::StreamInfo& info,
                              const Http::RequestHeaderMap* request_headers,
                              const Http::ResponseHeaderMap* response_headers,
                              const Http::ResponseTrailerMap* response_trailers) PURE;

  virtual void registerFunctions(CelFunctionRegistry* registry) const PURE;

  virtual ~CustomCelVocabulary() {}

  void setRequestHeaders(const Http::RequestHeaderMap* request_headers) {
    request_headers_ = request_headers;
  }
  void setResponseHeaders(const Http::ResponseHeaderMap* response_headers) {
    response_headers_ = response_headers;
  }
  void setResponseTrailers(const Http::ResponseTrailerMap* response_trailers) {
    response_trailers_ = response_trailers;
  }

private:
  const Http::RequestHeaderMap* request_headers_;
  const Http::ResponseHeaderMap* response_headers_;
  const Http::ResponseTrailerMap* response_trailers_;
};

using CustomCelVocabularyPtr = std::unique_ptr<CustomCelVocabulary>;

class CustomCelVocabularyFactory : public Envoy::Config::TypedFactory {
public:
  std::string category() const override { return "envoy.expr.custom_cel_vocabulary_config"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  virtual CustomCelVocabularyPtr
  createCustomCelVocabulary(const Protobuf::Message& config,
                            ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
