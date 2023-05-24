#pragma once

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/matcher/cel_matcher.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Http {
namespace Matching {

using ::Envoy::Extensions::Common::Matcher::CelMatchData;
using ::Envoy::Extensions::Filters::Common::Expr::StreamActivation;

class HttpCelDataInput : public Matcher::DataInput<Envoy::Http::HttpMatchingData> {
public:
  HttpCelDataInput() = default;
  Matcher::DataInputGetResult get(const Envoy::Http::HttpMatchingData& data) const override {
    RequestHeaderMapOptConstRef maybe_request_headers = data.requestHeaders();
    ResponseHeaderMapOptConstRef maybe_response_headers = data.responseHeaders();
    ResponseTrailerMapOptConstRef maybe_response_trailers = data.responseTrailers();
    // Returns NotAvailable state when all of three are empty. CEL matcher can support mix matching
    // condition of request headers, response headers and response trailers.
    if (!maybe_request_headers && !maybe_response_headers && !maybe_response_trailers) {
      std::cout << "tyxia not available \n";
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }

    std::unique_ptr<google::api::expr::runtime::BaseActivation> activation =
        Extensions::Filters::Common::Expr::createActivation(
            data.streamInfo(), maybe_request_headers.ptr(), maybe_response_headers.ptr(),
            maybe_response_trailers.ptr());

    // TODO(tyxia) probably never hit!!
    if (activation == nullptr) {
      std::cout << "tyxia not available 2 \n";
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }
    std::unique_ptr<StreamActivation> stream_activation =
        absl::WrapUnique(static_cast<StreamActivation*>(activation.release()));

    static_assert(std::is_move_constructible<StreamActivation>::value, "not move constructible");
    static_assert(std::is_move_assignable<StreamActivation>::value, "not move assignable");

    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::make_unique<CelMatchData>(std::move(*stream_activation))};
  }

  virtual absl::string_view dataInputType() const override { return "cel_data_input"; }
};

class HttpCelDataInputFactory : public Matcher::DataInputFactory<Envoy::Http::HttpMatchingData> {
public:
  HttpCelDataInputFactory() = default;
  std::string name() const override { return "envoy.matching.inputs.cel_data_input"; }

  Matcher::DataInputFactoryCb<Envoy::Http::HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<HttpCelDataInput>(); };
  }

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::HttpAttributesCelMatchInput>();
  }
};

DECLARE_FACTORY(HttpCelDataInputFactory);

} // namespace Matching
} // namespace Http
} // namespace Envoy
