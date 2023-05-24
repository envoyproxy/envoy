#pragma once

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"

// #include "source/common/matcher/cel_matcher.h"
// #include "source/extensions/filters/common/expr/evaluator.h"

#include "xds/type/matcher/v3/http_inputs.pb.h"

namespace Envoy {
namespace Http {
namespace Matching {

// using Envoy::Extensions::Filters::Common::Expr::StreamActivation;

/**
 * Common base class for all the header/trailer DataInputs.
 */
template <class HeaderType>
class HttpHeadersDataInputBase : public Matcher::DataInput<HttpMatchingData> {
public:
  explicit HttpHeadersDataInputBase(const std::string& name) : name_(name) {}

  virtual OptRef<const HeaderType> headerMap(const HttpMatchingData& data) const PURE;

  Matcher::DataInputGetResult get(const HttpMatchingData& data) const override {
    const OptRef<const HeaderType> maybe_headers = headerMap(data);

    if (!maybe_headers) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }

    auto header_string = HeaderUtility::getAllOfHeaderAsString(*maybe_headers, name_, ",");

    if (header_string.result()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              std::string(header_string.result().value())};
    }

    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
  }

private:
  const LowerCaseString name_;
};

/**
 * Common base class for all the header/trailer DataInputsFactory.
 */
template <class DataInputType, class ProtoType>
class HttpHeadersDataInputFactoryBase : public Matcher::DataInputFactory<HttpMatchingData> {
public:
  explicit HttpHeadersDataInputFactoryBase(const std::string& name) : name_(name) {}

  std::string name() const override { return "envoy.matching.inputs." + name_; }

  Matcher::DataInputFactoryCb<HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const ProtoType&>(config, validation_visitor);

    return [header_name = typed_config.header_name()] {
      return std::make_unique<DataInputType>(header_name);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

class HttpRequestHeadersDataInput : public HttpHeadersDataInputBase<RequestHeaderMap> {
public:
  explicit HttpRequestHeadersDataInput(const std::string& name) : HttpHeadersDataInputBase(name) {}

  OptRef<const RequestHeaderMap> headerMap(const HttpMatchingData& data) const override {
    return data.requestHeaders();
  }
};

class HttpRequestHeadersDataInputFactory
    : public HttpHeadersDataInputFactoryBase<
          HttpRequestHeadersDataInput, envoy::type::matcher::v3::HttpRequestHeaderMatchInput> {
public:
  HttpRequestHeadersDataInputFactory() : HttpHeadersDataInputFactoryBase("request_headers") {}
};

DECLARE_FACTORY(HttpRequestHeadersDataInputFactory);

class HttpResponseHeadersDataInput : public HttpHeadersDataInputBase<ResponseHeaderMap> {
public:
  explicit HttpResponseHeadersDataInput(const std::string& name) : HttpHeadersDataInputBase(name) {}

  OptRef<const ResponseHeaderMap> headerMap(const HttpMatchingData& data) const override {
    return data.responseHeaders();
  }
};

class HttpResponseHeadersDataInputFactory
    : public HttpHeadersDataInputFactoryBase<
          HttpResponseHeadersDataInput, envoy::type::matcher::v3::HttpResponseHeaderMatchInput> {
public:
  HttpResponseHeadersDataInputFactory() : HttpHeadersDataInputFactoryBase("response_headers") {}
};

DECLARE_FACTORY(HttpResponseHeadersDataInputFactory);

class HttpRequestTrailersDataInput : public HttpHeadersDataInputBase<RequestTrailerMap> {
public:
  explicit HttpRequestTrailersDataInput(const std::string& name) : HttpHeadersDataInputBase(name) {}

  OptRef<const RequestTrailerMap> headerMap(const HttpMatchingData& data) const override {
    return data.requestTrailers();
  }
};

class HttpRequestTrailersDataInputFactory
    : public HttpHeadersDataInputFactoryBase<
          HttpRequestTrailersDataInput, envoy::type::matcher::v3::HttpRequestTrailerMatchInput> {
public:
  HttpRequestTrailersDataInputFactory() : HttpHeadersDataInputFactoryBase("request_trailers") {}
};

DECLARE_FACTORY(HttpRequestTrailersDataInputFactory);

class HttpResponseTrailersDataInput : public HttpHeadersDataInputBase<ResponseTrailerMap> {
public:
  explicit HttpResponseTrailersDataInput(const std::string& name)
      : HttpHeadersDataInputBase(name) {}

  OptRef<const ResponseTrailerMap> headerMap(const HttpMatchingData& data) const override {
    return data.responseTrailers();
  }
};

class HttpResponseTrailersDataInputFactory
    : public HttpHeadersDataInputFactoryBase<
          HttpResponseTrailersDataInput, envoy::type::matcher::v3::HttpResponseTrailerMatchInput> {
public:
  HttpResponseTrailersDataInputFactory() : HttpHeadersDataInputFactoryBase("response_trailers") {}
};

DECLARE_FACTORY(HttpResponseTrailersDataInputFactory);

class HttpRequestQueryParamsDataInput : public Matcher::DataInput<HttpMatchingData> {
public:
  explicit HttpRequestQueryParamsDataInput(const std::string& query_param)
      : query_param_(query_param) {}

  Matcher::DataInputGetResult get(const HttpMatchingData& data) const override {
    const auto maybe_headers = data.requestHeaders();

    if (!maybe_headers) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }

    const auto ret = maybe_headers->Path();
    if (!ret) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
    }

    Utility::QueryParams params =
        Http::Utility::parseAndDecodeQueryString(ret->value().getStringView());

    auto ItParam = params.find(query_param_);
    if (ItParam == params.end()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::monostate()};
    }
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::move(ItParam->second)};
  }

private:
  const std::string query_param_;
};

class HttpRequestQueryParamsDataInputFactory : public Matcher::DataInputFactory<HttpMatchingData> {
public:
  std::string name() const override { return "query_params"; }

  Matcher::DataInputFactoryCb<HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message& config,
                           ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::type::matcher::v3::HttpRequestQueryParamMatchInput&>(config,
                                                                          validation_visitor);

    return [query_param = typed_config.query_param()] {
      return std::make_unique<HttpRequestQueryParamsDataInput>(query_param);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::HttpRequestQueryParamMatchInput>();
  }
};

DECLARE_FACTORY(HttpRequestQueryParamsDataInputFactory);

// class HttpCelDataInput : public Matcher::DataInput<Envoy::Http::HttpMatchingData> {
// public:
//   HttpCelDataInput() = default;
//   Matcher::DataInputGetResult get(const Envoy::Http::HttpMatchingData& data) const override {
//     RequestHeaderMapOptConstRef maybe_request_headers = data.requestHeaders();
//     ResponseHeaderMapOptConstRef maybe_response_headers = data.responseHeaders();
//     ResponseTrailerMapOptConstRef maybe_response_trailers = data.responseTrailers();
//     // Returns NotAvailable state when all of three are empty. CEL matcher can support mix
//     matching
//     // condition of request headers, response headers and response trailers.
//     if (!maybe_request_headers && !maybe_response_headers && !maybe_response_trailers) {
//       return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
//     }

//     std::unique_ptr<google::api::expr::runtime::BaseActivation> activation =
//         Extensions::Filters::Common::Expr::createActivation(
//             data.streamInfo(), maybe_request_headers.ptr(), maybe_response_headers.ptr(),
//             maybe_response_trailers.ptr());

//     // TODO(tyxia) probably never hit!!
//     if (activation == nullptr) {
//       return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::monostate()};
//     }
//     std::unique_ptr<StreamActivation> stream_activation =
//         absl::WrapUnique(static_cast<StreamActivation*>(activation.release()));

//     static_assert(std::is_move_constructible<StreamActivation>::value, "not move constructible");
//     static_assert(std::is_move_assignable<StreamActivation>::value, "not move assignable");

//     return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
//             std::make_unique<Matcher::CelMatchData>(std::move(*stream_activation))};
//   }

//   virtual absl::string_view dataInputType() const override { return "cel_data_input"; }
//   // TODO(tyxia) remove
// private:
// };

// class HttpCelDataInputFactory : public Matcher::DataInputFactory<Envoy::Http::HttpMatchingData> {
// public:
//   HttpCelDataInputFactory() = default;
//   std::string name() const override { return "envoy.matching.inputs.cel_data_input"; }

//   virtual Matcher::DataInputFactoryCb<Envoy::Http::HttpMatchingData>
//   createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&)
//   override {
//     return [] { return std::make_unique<HttpCelDataInput>(); };
//   }

//   virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override {
//     return std::make_unique<xds::type::matcher::v3::HttpAttributesCelMatchInput>();
//   }

// private:
// };

// DECLARE_FACTORY(HttpCelDataInputFactory);
} // namespace Matching
} // namespace Http
} // namespace Envoy
