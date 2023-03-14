#pragma once

#include <string>

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/status_code_input.pb.h"
#include "envoy/type/matcher/v3/status_code_input.pb.validate.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

// TODO(tyxia) This could be int
template <class ResultDataType = std::string>
class HttpResponseStatusCodeInput : public Matcher::DataInput<HttpMatchingData, ResultDataType> {
public:
  HttpResponseStatusCodeInput() = default;
  ~HttpResponseStatusCodeInput() override = default;

  Matcher::DataInputGetResult<ResultDataType> get(const HttpMatchingData& data) const override {
    const auto maybe_headers = data.responseHeaders();

    if (!maybe_headers) {
      return {Matcher::DataAvailability::NotAvailable, absl::nullopt};
    }
    const auto maybe_status = Http::Utility::getResponseStatusOrNullopt(*maybe_headers);

    if (maybe_status.has_value()) {
      return {Matcher::DataAvailability::AllDataAvailable, absl::StrCat(*maybe_status)};
    }

    return {Matcher::DataAvailability::AllDataAvailable, absl::nullopt};
  }
};

template <class ResultDataType = std::string>
class HttpResponseStatusCodeClassInput
    : public Matcher::DataInput<HttpMatchingData, ResultDataType> {
public:
  HttpResponseStatusCodeClassInput() = default;
  ~HttpResponseStatusCodeClassInput() override = default;

  Matcher::DataInputGetResult<ResultDataType> get(const HttpMatchingData& data) const override {
    const auto maybe_headers = data.responseHeaders();
    if (!maybe_headers) {
      return {Matcher::DataAvailability::NotAvailable, absl::nullopt};
    }

    const auto maybe_status = Http::Utility::getResponseStatusOrNullopt(*maybe_headers);
    if (maybe_status.has_value()) {
      if (*maybe_status >= 100 && *maybe_status < 200) {
        return {Matcher::DataAvailability::AllDataAvailable, "1xx"};
      }
      if (*maybe_status >= 200 && *maybe_status < 300) {
        return {Matcher::DataAvailability::AllDataAvailable, "2xx"};
      }
      if (*maybe_status >= 300 && *maybe_status < 400) {
        return {Matcher::DataAvailability::AllDataAvailable, "3xx"};
      }
      if (*maybe_status >= 400 && *maybe_status < 500) {
        return {Matcher::DataAvailability::AllDataAvailable, "4xx"};
      }
      if (*maybe_status >= 500 && *maybe_status < 600) {
        return {Matcher::DataAvailability::AllDataAvailable, "5xx"};
      }
    }
    return {Matcher::DataAvailability::AllDataAvailable, absl::nullopt};
  }
};

template <class DataInputType, class ProtoType, class ResultDataType = std::string>
class HttpResponseStatusCodeInputFactoryBase
    : public Matcher::DataInputFactory<HttpMatchingData, ResultDataType> {
public:
  explicit HttpResponseStatusCodeInputFactoryBase(const std::string& name)
      : name_("envoy.matching.inputs." + name) {}

  std::string name() const override { return name_; }

  Matcher::DataInputFactoryCb<HttpMatchingData, ResultDataType>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {

    return [] { return std::make_unique<DataInputType>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

// TODO(tyxia) Add support to register as templated type factory.
class HttpResponseStatusCodeInputFactory
    : public HttpResponseStatusCodeInputFactoryBase<
          HttpResponseStatusCodeInput<std::string>,
          envoy::type::matcher::v3::HttpResponseStatusCodeMatchInput> {
public:
  explicit HttpResponseStatusCodeInputFactory()
      : HttpResponseStatusCodeInputFactoryBase("status_code_input") {}
};

class HttpResponseStatusCodeClassInputFactory
    : public HttpResponseStatusCodeInputFactoryBase<
          HttpResponseStatusCodeClassInput<std::string>,
          envoy::type::matcher::v3::HttpResponseStatusCodeClassMatchInput> {
public:
  explicit HttpResponseStatusCodeClassInputFactory()
      : HttpResponseStatusCodeInputFactoryBase("status_code_class_input") {}
};

} // namespace Matching
} // namespace Http
} // namespace Envoy
