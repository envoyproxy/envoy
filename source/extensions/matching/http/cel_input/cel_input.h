#pragma once

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace CelInput {

using ::Envoy::Http::RequestHeaderMapOptConstRef;
using ::Envoy::Http::ResponseHeaderMapOptConstRef;
using ::Envoy::Http::ResponseTrailerMapOptConstRef;

using BaseActivationPtr = std::unique_ptr<google::api::expr::runtime::BaseActivation>;

// CEL matcher specific matching data
class CelMatchData : public ::Envoy::Matcher::CustomMatchData {
public:
  explicit CelMatchData(BaseActivationPtr activation) : activation_(std::move(activation)) {}
  BaseActivationPtr activation_;
};

class HttpCelDataInput : public Matcher::DataInput<Envoy::Http::HttpMatchingData> {
public:
  HttpCelDataInput() = default;
  Matcher::DataInputGetResult get(const Envoy::Http::HttpMatchingData& data) const override {
    RequestHeaderMapOptConstRef maybe_request_headers = data.requestHeaders();
    ResponseHeaderMapOptConstRef maybe_response_headers = data.responseHeaders();
    ResponseTrailerMapOptConstRef maybe_response_trailers = data.responseTrailers();

    // CEL library supports mixed matching of request/response attributes(e.g., headers, trailers)
    // and attributes from stream info.
    std::unique_ptr<google::api::expr::runtime::BaseActivation> activation =
        Extensions::Filters::Common::Expr::createActivation(
            nullptr, // TODO: pass local_info to CEL activation.
            data.streamInfo(), maybe_request_headers.ptr(), maybe_response_headers.ptr(),
            maybe_response_trailers.ptr());

    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::make_unique<CelMatchData>(std::move(activation))};
  }

  absl::string_view dataInputType() const override { return "cel_data_input"; }
};

class HttpCelDataInputFactory : public Matcher::DataInputFactory<Envoy::Http::HttpMatchingData> {
public:
  HttpCelDataInputFactory() = default;
  std::string name() const override { return "envoy.matching.inputs.cel_data_input"; }

  Matcher::DataInputFactoryCb<Envoy::Http::HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<HttpCelDataInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::HttpAttributesCelMatchInput>();
  }
};

DECLARE_FACTORY(HttpCelDataInputFactory);

} // namespace CelInput
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
