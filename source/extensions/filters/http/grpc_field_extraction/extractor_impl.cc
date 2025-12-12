#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "source/common/common/logger.h"

#include "proto_field_extraction/field_value_extractor/field_value_extractor_factory.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

using Protobuf::field_extraction::FieldValueExtractorFactory;

} // namespace

absl::StatusOr<ExtractorImpl> ExtractorImpl::create(
    const TypeFinder& type_finder, absl::string_view request_type_url,
    const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
        field_extractions) {
  ExtractorImpl extractor;
  const absl::Status status = extractor.init(type_finder, request_type_url, field_extractions);
  RETURN_IF_NOT_OK(status);
  return extractor;
}

absl::Status ExtractorImpl::init(
    const TypeFinder& type_finder, absl::string_view request_type_url,
    const envoy::extensions::filters::http::grpc_field_extraction::v3::FieldExtractions&
        field_extractions) {
  FieldValueExtractorFactory extractor_factory(type_finder);
  for (const auto& it : field_extractions.request_field_extractions()) {
    auto extractor_or_error = extractor_factory.Create(request_type_url, it.first);
    RETURN_IF_NOT_OK(extractor_or_error.status());
    per_field_extractors_.emplace(it.first, std::move(extractor_or_error.value()));
  }
  return absl::OkStatus();
}

absl::StatusOr<ExtractionResult>
ExtractorImpl::processRequest(Protobuf::field_extraction::MessageData& message) const {

  ExtractionResult result;
  for (const auto& it : per_field_extractors_) {
    absl::StatusOr<ProtobufWkt::Value> extracted_value = it.second->ExtractValue(message);
    if (!extracted_value.ok()) {
      return extracted_value.status();
    }

    ENVOY_LOG_MISC(debug, "extracted the following resource values from the {} field: {}", it.first,
                   extracted_value->DebugString());
    result.push_back({it.first, std::move(*extracted_value)});
  }

  return result;
}

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
