#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "source/common/common/logger.h"

#include "proto_field_extraction/field_value_extractor/field_value_extractor_factory.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_interface.h"

namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction {
namespace {

using google::protobuf::field_extraction::FieldValueExtractorFactory;
using google::protobuf::field_extraction::FieldValueExtractorInterface;

} // namespace

absl::Status ExtractorImpl::Init() {
  FieldValueExtractorFactory extractor_factory(type_finder_);
  for (const auto& it : field_extractions_.request_field_extractions()) {
    auto extractor = extractor_factory.Create(request_type_url_, it.first);
    if (!extractor.ok()) {
      return extractor.status();
    }

    per_field_extractors.emplace(it.first, std::move(extractor.value()));
  }
  return absl::OkStatus();
}

absl::StatusOr<ExtractionResult>
ExtractorImpl::ProcessRequest(google::protobuf::field_extraction::MessageData& message) const {

  ExtractionResult result;
  for (const auto& it : per_field_extractors) {
    auto extracted_values = it.second->Extract(message);
    if (!extracted_values.ok()) {
      return extracted_values.status();
    }

    ENVOY_LOG_MISC(
        debug, "extracted the following resource values from the {} field: {}", request_type_url_,
        std::accumulate(extracted_values.value().begin(), extracted_values.value().end(),
                        std::string(),
                        [](const std::string& lhs, const std::string& rhs) { return lhs + rhs; }));
    result.push_back({it.first, std::move(extracted_values.value())});
  }

  return result;
}

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
