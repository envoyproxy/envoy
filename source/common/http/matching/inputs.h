#pragma once

#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/http_inputs.pb.h"
#include "envoy/type/matcher/v3/http_inputs.pb.validate.h"

#include "source/common/http/header_utility.h"

namespace Envoy {
namespace Http {
namespace Matching {
/**
 * Common base class for all the header/trailer DataInputs.
 */
template <class MatchingDataType, class HeaderType>
class HttpHeadersDataInputBase : public Matcher::DataInput<MatchingDataType> {
public:
  explicit HttpHeadersDataInputBase(const std::string& name) : name_(name) {}

  virtual absl::optional<std::reference_wrapper<const HeaderType>>
  headerMap(const MatchingDataType& data) const PURE;

  Matcher::DataInputGetResult get(const MatchingDataType& data) const override {
    const auto maybe_headers = headerMap(data);

    if (!maybe_headers) {
      return {Matcher::DataInputGetResult::DataAvailability::NotAvailable, absl::nullopt};
    }

    auto header_string = HeaderUtility::getAllOfHeaderAsString(maybe_headers->get(), name_, ",");

    if (header_string.result()) {
      return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              std::string(header_string.result().value())};
    }

    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, absl::nullopt};
  }

private:
  const LowerCaseString name_;
};

/**
 * Common base class for all the header/trailer DataInputsFactory.
 */
template <class MatchingDataType, class DataInputType, class ProtoType>
class HttpHeadersDataInputFactoryBase : public Matcher::DataInputFactory<MatchingDataType> {
public:
  explicit HttpHeadersDataInputFactoryBase(const std::string& name) : name_(name) {}

  std::string name() const override { return "envoy.matching.inputs." + name_; }

  Matcher::DataInputFactoryCb<MatchingDataType>
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

template <class MatchingDataType>
class HttpRequestHeadersDataInput
    : public HttpHeadersDataInputBase<MatchingDataType, RequestHeaderMap> {
public:
  explicit HttpRequestHeadersDataInput(const std::string& name)
      : HttpHeadersDataInputBase<MatchingDataType, RequestHeaderMap>(name) {}

  absl::optional<std::reference_wrapper<const RequestHeaderMap>>
  headerMap(const MatchingDataType& data) const override {
    return data.requestHeaders();
  }
};

template <class MatchingDataType>
class HttpRequestHeadersDataInputFactoryBase
    : public HttpHeadersDataInputFactoryBase<
          MatchingDataType, HttpRequestHeadersDataInput<MatchingDataType>,
          envoy::type::matcher::v3::HttpRequestHeaderMatchInput> {
public:
  HttpRequestHeadersDataInputFactoryBase()
      : HttpHeadersDataInputFactoryBase<MatchingDataType,
                                        HttpRequestHeadersDataInput<MatchingDataType>,
                                        envoy::type::matcher::v3::HttpRequestHeaderMatchInput>(
            "request_headers") {}
};

template <class MatchingDataType>
class HttpResponseHeadersDataInput
    : public HttpHeadersDataInputBase<MatchingDataType, ResponseHeaderMap> {
public:
  explicit HttpResponseHeadersDataInput(const std::string& name)
      : HttpHeadersDataInputBase<MatchingDataType, ResponseHeaderMap>(name) {}

  absl::optional<std::reference_wrapper<const ResponseHeaderMap>>
  headerMap(const MatchingDataType& data) const override {
    return data.responseHeaders();
  }
};

template <class MatchingDataType>
class HttpResponseHeadersDataInputFactoryBase
    : public HttpHeadersDataInputFactoryBase<
          MatchingDataType, HttpResponseHeadersDataInput<MatchingDataType>,
          envoy::type::matcher::v3::HttpResponseHeaderMatchInput> {
public:
  HttpResponseHeadersDataInputFactoryBase()
      : HttpHeadersDataInputFactoryBase<MatchingDataType,
                                        HttpResponseHeadersDataInput<MatchingDataType>,
                                        envoy::type::matcher::v3::HttpResponseHeaderMatchInput>(
            "response_headers") {}
};

template <class MatchingDataType>
class HttpRequestTrailersDataInput
    : public HttpHeadersDataInputBase<MatchingDataType, RequestTrailerMap> {
public:
  explicit HttpRequestTrailersDataInput(const std::string& name)
      : HttpHeadersDataInputBase<MatchingDataType, RequestTrailerMap>(name) {}

  absl::optional<std::reference_wrapper<const RequestTrailerMap>>
  headerMap(const MatchingDataType& data) const override {
    return data.requestTrailers();
  }
};

template <class MatchingDataType>
class HttpRequestTrailersDataInputFactoryBase
    : public HttpHeadersDataInputFactoryBase<
          MatchingDataType, HttpRequestTrailersDataInput<MatchingDataType>,
          envoy::type::matcher::v3::HttpRequestTrailerMatchInput> {
public:
  HttpRequestTrailersDataInputFactoryBase()
      : HttpHeadersDataInputFactoryBase<MatchingDataType,
                                        HttpRequestTrailersDataInput<MatchingDataType>,
                                        envoy::type::matcher::v3::HttpRequestTrailerMatchInput>(
            "request_trailers") {}
};

template <class MatchingDataType>
class HttpResponseTrailersDataInput
    : public HttpHeadersDataInputBase<MatchingDataType, ResponseTrailerMap> {
public:
  explicit HttpResponseTrailersDataInput(const std::string& name)
      : HttpHeadersDataInputBase<MatchingDataType, ResponseTrailerMap>(name) {}

  absl::optional<std::reference_wrapper<const ResponseTrailerMap>>
  headerMap(const MatchingDataType& data) const override {
    return data.responseTrailers();
  }
};

template <class MatchingDataType>
class HttpResponseTrailersDataInputFactoryBase
    : public HttpHeadersDataInputFactoryBase<
          MatchingDataType, HttpRequestTrailersDataInput<MatchingDataType>,
          envoy::type::matcher::v3::HttpRequestTrailerMatchInput> {
public:
  HttpResponseTrailersDataInputFactoryBase()
      : HttpHeadersDataInputFactoryBase<MatchingDataType,
                                        HttpRequestTrailersDataInput<MatchingDataType>,
                                        envoy::type::matcher::v3::HttpRequestTrailerMatchInput>(
            "response_trailers") {}
};
} // namespace Matching
} // namespace Http
} // namespace Envoy
