#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/http/ext_proc/on_processing_response.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class ExtProcTestUtility {
public:
  // Compare a reference header map to a proto
  static bool headerProtosEqualIgnoreOrder(const ::Envoy::Http::HeaderMap& expected,
                                           const envoy::config::core::v3::HeaderMap& actual);

private:
  // These headers are present in the actual, but cannot be specified in the expected
  // ignoredHeaders should not be used for equal comparison
  static const absl::flat_hash_set<std::string> ignoredHeaders();
};

MATCHER_P(HeaderProtosEqual, expected, "HTTP header protos match") {
  return ExtProcTestUtility::headerProtosEqualIgnoreOrder(expected, arg);
}

template <typename... Args>
inline void verifyMultipleHeaderValues(const Envoy::Http::HeaderMap& headers,
                                       Envoy::Http::LowerCaseString const& key, Args... values) {
  static constexpr size_t num_values = sizeof...(Args);
  static_assert(num_values > 0);
  const auto hdr = headers.get(key);
  EXPECT_EQ(hdr.size(), num_values);
  if (hdr.size() != num_values) {
    return;
  }
  std::vector<absl::string_view> header_values;
  for (size_t idx = 0; idx < num_values; ++idx) {
    header_values.push_back(hdr[idx]->value().getStringView());
  }
  EXPECT_THAT(header_values, testing::UnorderedElementsAreArray({
                                 values...,
                             }));
}

MATCHER_P2(SingleProtoHeaderValueIs, key, value,
           absl::StrFormat("Header \"%s\" equals \"%s\"", key, value)) {
  for (const auto& hdr : arg.headers()) {
    if (key == hdr.key()) {
      return value == hdr.value();
    }
  }
  return false;
}

envoy::config::core::v3::HeaderValue makeHeaderValue(const std::string& key,
                                                     const std::string& value);

class TestOnProcessingResponse : public OnProcessingResponse {
public:
  void afterProcessingRequestHeaders(const envoy::service::ext_proc::v3::HeadersResponse& response,
                                     absl::Status processing_status,
                                     Envoy::StreamInfo::StreamInfo&) override;

  void afterProcessingResponseHeaders(const envoy::service::ext_proc::v3::HeadersResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingRequestBody(const envoy::service::ext_proc::v3::BodyResponse&, absl::Status,
                                  Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseBody(const envoy::service::ext_proc::v3::BodyResponse&, absl::Status,
                                   Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingRequestTrailers(const envoy::service::ext_proc::v3::TrailersResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseTrailers(const envoy::service::ext_proc::v3::TrailersResponse&,
                                       absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void
  afterReceivingImmediateResponse(const envoy::service::ext_proc::v3::ImmediateResponse& response,
                                  absl::Status processing_status,
                                  Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingStreamingImmediateResponse(
      const envoy::service::ext_proc::v3::StreamedImmediateResponse& response,
      absl::Status processing_status, Envoy::StreamInfo::StreamInfo&) override;

private:
  Envoy::Protobuf::Struct
  getHeaderMutations(const envoy::service::ext_proc::v3::HeaderMutation& header_mutation) {
    Envoy::Protobuf::Struct struct_metadata;
    for (auto& header : header_mutation.set_headers()) {
      Envoy::Protobuf::Value value;
      value.mutable_string_value()->assign(header.header().raw_value());
      struct_metadata.mutable_fields()->insert(std::make_pair(header.header().key(), value));
    }
    for (auto& header : header_mutation.remove_headers()) {
      Envoy::Protobuf::Value value;
      value.mutable_string_value()->assign("remove");
      struct_metadata.mutable_fields()->insert(std::make_pair(header, value));
    }
    return struct_metadata;
  }
  Envoy::Protobuf::Struct
  getBodyMutation(const envoy::service::ext_proc::v3::BodyMutation& body_mutation) {
    Envoy::Protobuf::Struct struct_metadata;
    if (body_mutation.has_body()) {
      Envoy::Protobuf::Value value;
      value.mutable_string_value()->assign(body_mutation.body());
      struct_metadata.mutable_fields()->insert(std::make_pair("body", value));
    } else {
      Envoy::Protobuf::Value value;
      value.mutable_string_value()->assign(absl::StrCat(body_mutation.clear_body()));
      struct_metadata.mutable_fields()->insert(std::make_pair("clear_body", value));
    }
    return struct_metadata;
  }
  Envoy::Protobuf::Struct makeStreamedImmediateResponseMetadata(
      const envoy::service::ext_proc::v3::StreamedImmediateResponse& response);
};

class TestOnProcessingResponseFactory : public OnProcessingResponseFactory {
public:
  ~TestOnProcessingResponseFactory() override = default;
  std::unique_ptr<OnProcessingResponse>
  createOnProcessingResponse(const Protobuf::Message&,
                             Envoy::Server::Configuration::CommonFactoryContext&,
                             const std::string&) const override {
    return std::make_unique<TestOnProcessingResponse>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::Protobuf::Struct()};
  }

  std::string name() const override { return "on_processing_response"; }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
