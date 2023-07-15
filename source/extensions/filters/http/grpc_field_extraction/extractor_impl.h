#pragma once

#include <string>

#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"

#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"

#include "absl/status/status.h"
#include "google/protobuf/descriptor.pb.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_factory.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {

using FieldValueExtractorPtr = std::unique_ptr<google::protobuf::field_extraction::FieldValueExtractorInterface>;
class ExtractorImpl : public Extractor {
public:
  explicit ExtractorImpl(
      TypeFinder type_finder, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions)
      : type_finder_(type_finder), request_type_url_(request_type_url),
        field_extractions_(field_extractions) {}

  absl::StatusOr<ExtractionResult> ProcessRequest(google::protobuf::field_extraction::MessageData& message) const override;

  absl::Status Init();

private:
  TypeFinder type_finder_;

  std::string request_type_url_;

  const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
      field_extractions_;

  absl::flat_hash_map<absl::string_view, FieldValueExtractorPtr> per_field_extractors;
};

class ExtractorFactoryImpl : public ExtractorFactory {
public:
  absl::StatusOr<ExtractorPtr>  CreateExtractor(
      TypeFinder type_finder, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions) const {
    auto extractor = std::make_unique<ExtractorImpl>(type_finder, request_type_url, field_extractions);
    auto status = extractor->Init();
    if (!status.ok()) {
      return  status;
    }
    return extractor;
  }
};

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
