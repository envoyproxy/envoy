#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/stream_info/filter_state.h"
#include "source/extensions/filters/http/ext_proc/on_processing_response.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

namespace Http {
namespace ExternalProcessing {

struct SaveProcessingResponseFilterSate
    : public std::enable_shard_from_this<SaveProcessingResponseFilterSate>,
      public StreamInfo::FilterState {
  struct Response {
    absl::Status processing_status;
    envoy::service::ext_proc::v3::ProcessingResponse processing_response;
  };
  std::vector<Response> responses;
};

class SaveProcessingResponse : public OnProcessingResponse {
public:
  void
  afterProcessingRequestHeaders(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                absl::Status processing_status,
                                Envoy::StreamInfo::StreamInfo&) override;

  void afterProcessingResponseHeaders(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingRequestBody(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                  absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseBody(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                   absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingRequestTrailers(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                      absl::Status, Envoy::StreamInfo::StreamInfo&) override;
  void afterProcessingResponseTrailers(const envoy::service::ext_proc::v3::ProcessingResponse&,
                                       absl::Status, Envoy::StreamInfo::StreamInfo&) override;

private:
  Envoy::ProtobufWkt::Struct
  getHeaderMutations(const envoy::service::ext_proc::v3::HeaderMutation& header_mutation) {
    Envoy::ProtobufWkt::Struct struct_metadata;
    for (auto& header : header_mutation.set_headers()) {
      Envoy::ProtobufWkt::Value value;
      value.mutable_string_value()->assign(header.header().raw_value());
      struct_metadata.mutable_fields()->insert(std::make_pair(header.header().key(), value));
    }
    for (auto& header : header_mutation.remove_headers()) {
      Envoy::ProtobufWkt::Value value;
      value.mutable_string_value()->assign("remove");
      struct_metadata.mutable_fields()->insert(std::make_pair(header, value));
    }
    return struct_metadata;
  }
  Envoy::ProtobufWkt::Struct
  getBodyMutation(const envoy::service::ext_proc::v3::BodyMutation& body_mutation) {
    Envoy::ProtobufWkt::Struct struct_metadata;
    if (body_mutation.has_body()) {
      Envoy::ProtobufWkt::Value value;
      value.mutable_string_value()->assign(body_mutation.body());
      struct_metadata.mutable_fields()->insert(std::make_pair("body", value));
    } else {
      Envoy::ProtobufWkt::Value value;
      value.mutable_string_value()->assign(absl::StrCat(body_mutation.clear_body()));
      struct_metadata.mutable_fields()->insert(std::make_pair("clear_body", value));
    }
    return struct_metadata;
  }
};

class SaveProcessingResponseFactory : public OnProcessingResponseFactory {
public:
  ~SaveProcessingResponseFactory() override = default;
  std::unique_ptr<OnProcessingResponse> createOnProcessingResponse(
      const Protobuf::Message&,
      const Envoy::Server::Configuration::CommonFactoryContext&) const override {
    return std::make_unique<SaveProcessingResponse>();
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom filter config proto. This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "on_processing_response"; }
};
} // namespace ExternalProcessing
} // namespace Http
