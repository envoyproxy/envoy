#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"
#include "envoy/type/matcher/v3/status_code_input.pb.h"
#include "envoy/type/matcher/v3/status_code_input.pb.validate.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Http {
namespace Matching {

class HttpResponseStatusCodeInput : public Matcher::DataInput<HttpMatchingData> {
public:
  HttpResponseStatusCodeInput() = default;
  ~HttpResponseStatusCodeInput() override = default;

  Matcher::DataInputGetResult get(const HttpMatchingData& data) const override {
    const auto maybe_headers = data.responseHeaders();

    if (!maybe_headers) {
      return Matcher::DataInputGetResult::NoData(Matcher::DataAvailability::NotAvailable);
    }
    const auto maybe_status = Http::Utility::getResponseStatusOrNullopt(*maybe_headers);

    if (maybe_status.has_value()) {
      return Matcher::DataInputGetResult::CreateString(absl::StrCat(*maybe_status));
    }

    return Matcher::DataInputGetResult::NoData();
  }
};

inline constexpr absl::string_view Code1xx = "1xx";
inline constexpr absl::string_view Code2xx = "2xx";
inline constexpr absl::string_view Code3xx = "3xx";
inline constexpr absl::string_view Code4xx = "4xx";
inline constexpr absl::string_view Code5xx = "5xx";

class HttpResponseStatusCodeClassInput : public Matcher::DataInput<HttpMatchingData> {
public:
  HttpResponseStatusCodeClassInput() = default;
  ~HttpResponseStatusCodeClassInput() override = default;

  Matcher::DataInputGetResult get(const HttpMatchingData& data) const override {
    const auto maybe_headers = data.responseHeaders();
    if (!maybe_headers) {
      return Matcher::DataInputGetResult::NoData(Matcher::DataAvailability::NotAvailable);
    }

    const auto maybe_status = Http::Utility::getResponseStatusOrNullopt(*maybe_headers);
    if (maybe_status.has_value()) {
      if (*maybe_status >= 100 && *maybe_status < 200) {
        return Matcher::DataInputGetResult::CreateStringView(Code1xx);
      }
      if (*maybe_status >= 200 && *maybe_status < 300) {
        return Matcher::DataInputGetResult::CreateStringView(Code2xx);
      }
      if (*maybe_status >= 300 && *maybe_status < 400) {
        return Matcher::DataInputGetResult::CreateStringView(Code3xx);
      }
      if (*maybe_status >= 400 && *maybe_status < 500) {
        return Matcher::DataInputGetResult::CreateStringView(Code4xx);
      }
      if (*maybe_status >= 500 && *maybe_status < 600) {
        return Matcher::DataInputGetResult::CreateStringView(Code5xx);
      }
    }
    return Matcher::DataInputGetResult::NoData();
  }
};

template <class DataInputType, class ProtoType>
class HttpResponseStatusCodeInputFactoryBase : public Matcher::DataInputFactory<HttpMatchingData> {
public:
  explicit HttpResponseStatusCodeInputFactoryBase(const std::string& name)
      : name_("envoy.matching.inputs." + name) {}

  std::string name() const override { return name_; }

  Matcher::DataInputFactoryCb<HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {

    return [] { return std::make_unique<DataInputType>(); };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoType>();
  }

private:
  const std::string name_;
};

class HttpResponseStatusCodeInputFactory
    : public HttpResponseStatusCodeInputFactoryBase<
          HttpResponseStatusCodeInput, envoy::type::matcher::v3::HttpResponseStatusCodeMatchInput> {
public:
  explicit HttpResponseStatusCodeInputFactory()
      : HttpResponseStatusCodeInputFactoryBase("status_code_input") {}
};

class HttpResponseStatusCodeClassInputFactory
    : public HttpResponseStatusCodeInputFactoryBase<
          HttpResponseStatusCodeClassInput,
          envoy::type::matcher::v3::HttpResponseStatusCodeClassMatchInput> {
public:
  explicit HttpResponseStatusCodeClassInputFactory()
      : HttpResponseStatusCodeInputFactoryBase("status_code_class_input") {}
};

} // namespace Matching
} // namespace Http
} // namespace Envoy
