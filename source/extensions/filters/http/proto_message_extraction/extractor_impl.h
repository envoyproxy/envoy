#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/http/proto_message_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_message_extraction/v3/config.pb.validate.h"

#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor.h"
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor_interface.h"
#include "source/extensions/filters/http/proto_message_extraction/extractor.h"

#include "absl/status/statusor.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

class ExtractorImpl : public Extractor {
public:
  ExtractorImpl(
      const TypeFinder& type_finder, const google::grpc::transcoding::TypeHelper& type_helper,
      absl::string_view request_type_url, absl::string_view response_type_url,
      // const Envoy::ProtobufWkt::Type* request_type,
      // const Envoy::ProtobufWkt::Type* response_type,
      const envoy::extensions::filters::http::proto_message_extraction::v3::MethodExtraction&
          method_extraction)
      : method_extraction_(method_extraction), request_type_url_(request_type_url),
        response_type_url_(response_type_url), type_finder_(type_finder),
        type_helper_(type_helper) {}

  //  The init method should be invoked right after the constructor has been
  //  called.
  absl::Status init();

  void processRequest(Protobuf::field_extraction::MessageData& message) override;

  void processResponse(Protobuf::field_extraction::MessageData& message) override;

  const ExtractedMessageResult& GetResult() const override { return result_; }

private:
  const envoy::extensions::filters::http::proto_message_extraction::v3::MethodExtraction&
      method_extraction_;

  std::string request_type_url_;
  std::string response_type_url_;

  const TypeFinder& type_finder_;
  const google::grpc::transcoding::TypeHelper& type_helper_;

  FieldPathToExtractType request_field_path_to_extract_type_;

  FieldPathToExtractType response_field_path_to_extract_type_;

  ExtractedMessageResult result_;
  std::unique_ptr<ProtoExtractorInterface> request_extractor_;
  std::unique_ptr<ProtoExtractorInterface> response_extractor_;
};

class ExtractorFactoryImpl : public ExtractorFactory {
public:
  absl::StatusOr<ExtractorPtr> createExtractor(
      const google::grpc::transcoding::TypeHelper& type_helper, const TypeFinder& type_finder,
      std::string request_type_url, std::string response_type_url,
      const envoy::extensions::filters::http::proto_message_extraction::v3::MethodExtraction&
          method_extraction) const override {
    auto extractor = std::make_unique<ExtractorImpl>(type_finder, type_helper, request_type_url,
                                                     response_type_url, method_extraction);
    auto status = extractor->init();
    if (!status.ok()) {
      return status;
    }

    return extractor;
  }
};

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
