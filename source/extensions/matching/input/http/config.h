#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/matching/input/v3/http_input.pb.validate.h"

#include "common/http/matching_data.h"
#include "common/matcher/matcher.h"

#include "extensions/matching/input/http/inputs.h"

namespace Envoy {

class HttpRequestBodyFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpRequestBodyInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpRequestBody>(input.limit());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_request_body"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpRequestBodyInput>();
  }
};

class HttpResponseBodyFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpResponseBodyInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpResponseBody>(input.limit());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_response_body"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpResponseBodyInput>();
  }
};

class HttpRequestHeadersFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpRequestHeaderInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpRequestHeaders>(input.header());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_request_headers"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpRequestHeaderInput>();
  }
};

class HttpRequestTrailersFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpRequestTrailerInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpRequestTrailers>(input.header());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_request_trailers"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpRequestTrailerInput>();
  }
};

class HttpResponseTrailersFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpResponseTrailerInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpResponseTrailers>(input.header());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_response_trailers"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpResponseTrailerInput>();
  }
};

class HttpResponseHeadersFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) override {
    envoy::extensions::matching::input::v3::HttpResponseHeaderInput input;
    MessageUtil::unpackTo(config.typed_config(), input);
    return std::make_unique<HttpResponseHeaders>(input.header());
  }
  std::string name() const override { return "envoy.matcher.inputs.http_response_headers"; };
  std::string category() const override { return "bkag"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::matching::input::v3::HttpResponseHeaderInput>();
  }
};

template <class T> class FixedData : public DataInput<T> {
public:
  DataInputGetResult get(const T&) { return {false, false, ""}; }
};

class FixedDataInputFactory : public DataInputFactory<Http::HttpMatchingData> {
public:
  DataInputPtr<Http::HttpMatchingData>
  create(const envoy::config::core::v3::TypedExtensionConfig&) override {
    return std::make_unique<FixedData<Http::HttpMatchingData>>();
  }
  std::string name() const override { return "envoy.matcher.inputs.fixed"; };
  std::string category() const override { return "bkag"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Empty>();
  }
};

} // namespace Envoy