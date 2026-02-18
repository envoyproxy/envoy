#pragma once

#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/matching/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace DynamicModules {

/**
 * Dynamic module matcher specific matching data. This wraps the HTTP matching context
 * (request/response headers and trailers) so that the dynamic module can access them
 * via ABI callbacks during match evaluation.
 */
class DynamicModuleMatchData : public ::Envoy::Matcher::CustomMatchData {
public:
  const ::Envoy::Http::RequestHeaderMap* request_headers_{};
  const ::Envoy::Http::ResponseHeaderMap* response_headers_{};
  const ::Envoy::Http::ResponseTrailerMap* response_trailers_{};
};

/**
 * Data input that extracts HTTP request and response data from the matching context
 * and wraps it as DynamicModuleMatchData for consumption by the dynamic module matcher.
 */
class HttpDynamicModuleDataInput
    : public ::Envoy::Matcher::DataInput<::Envoy::Http::HttpMatchingData> {
public:
  HttpDynamicModuleDataInput() = default;

  ::Envoy::Matcher::DataInputGetResult
  get(const ::Envoy::Http::HttpMatchingData& data) const override {
    auto match_data = std::make_shared<DynamicModuleMatchData>();
    match_data->request_headers_ = data.requestHeaders().ptr();
    match_data->response_headers_ = data.responseHeaders().ptr();
    match_data->response_trailers_ = data.responseTrailers().ptr();
    return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::move(match_data)};
  }

  absl::string_view dataInputType() const override { return "dynamic_module_data_input"; }
};

class HttpDynamicModuleDataInputFactory
    : public ::Envoy::Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData> {
public:
  HttpDynamicModuleDataInputFactory() = default;
  std::string name() const override { return "envoy.matching.inputs.dynamic_module_data_input"; }

  ::Envoy::Matcher::DataInputFactoryCb<::Envoy::Http::HttpMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<HttpDynamicModuleDataInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::http::dynamic_modules::v3::HttpDynamicModuleMatchInput>();
  }
};

DECLARE_FACTORY(HttpDynamicModuleDataInputFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
