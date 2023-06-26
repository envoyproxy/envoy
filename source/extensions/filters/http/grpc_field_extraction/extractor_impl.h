#pragma once

#include <string>

#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"
#include "absl/status/status.h"
#include "grpc_transcoding/type_helper.h"
#include "google/protobuf/descriptor.pb.h"
#include "src/field_value_extractor/field_value_extractor_factory.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

class ExtractorImpl : public Extractor {
public:
  explicit ExtractorImpl(
      TypeFinder type_finder, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions)
      : type_finder_(type_finder), request_type_url_(request_type_url),
        field_extractions_(field_extractions) {}

  absl::Status ProcessRequest(google::protobuf::field_extraction::MessageData& message) override;

  const ExtractionResult& GetResult() const override { return result_; }

private:
  absl::StatusOr<RequestField> ExtractRequestField(
      google::protobuf::field_extraction::FieldValueExtractorFactory& extractor_factory,
      absl::string_view field_path, google::protobuf::field_extraction::MessageData& message) const;


  TypeFinder type_finder_;

  std::string request_type_url_;

  const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
      field_extractions_;

  ExtractionResult result_;

  int message_count_ = 0;
};

class ExtractorFactoryImpl : public ExtractorFactory {
public:
  ExtractorPtr CreateExtractor(
      TypeFinder type_finder, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions) const {
    return std::make_unique<ExtractorImpl>(type_finder, request_type_url, field_extractions);
  }
};

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
