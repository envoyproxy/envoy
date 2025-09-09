#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_field_extraction/v3/config.pb.validate.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/grpc_field_extraction/extractor.h"

#include "absl/status/status.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {

using FieldValueExtractorPtr =
    std::unique_ptr<Protobuf::field_extraction::FieldValueExtractorInterface>;
class ExtractorImpl : public Extractor {
public:
  static absl::StatusOr<ExtractorImpl>
  create(const TypeFinder& type_finder, absl::string_view request_type_url,
         const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
             field_extractions);

  absl::StatusOr<ExtractionResult>
  processRequest(Protobuf::field_extraction::MessageData& message) const override;

private:
  ExtractorImpl() = default;
  //  The init method should be invoked right after the constructor has been called.
  absl::Status
  init(const TypeFinder& type_finder, absl::string_view request_type_url,
       const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
           field_extractions);

  absl::flat_hash_map<absl::string_view, FieldValueExtractorPtr> per_field_extractors_;
};

class ExtractorFactoryImpl : public ExtractorFactory {
public:
  absl::StatusOr<ExtractorPtr> createExtractor(
      const TypeFinder& type_finder, absl::string_view request_type_url,
      const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
          field_extractions) const override {
    absl::StatusOr<ExtractorImpl> extractor =
        ExtractorImpl::create(type_finder, request_type_url, field_extractions);
    RETURN_IF_NOT_OK(extractor.status());
    return std::make_unique<ExtractorImpl>(std::move(extractor.value()));
  }
};

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
