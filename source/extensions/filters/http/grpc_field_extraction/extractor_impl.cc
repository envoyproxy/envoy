#include "source/extensions/filters/http/grpc_field_extraction/extractor_impl.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "source/common/common/logger.h"

#include "absl/strings/str_format.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_factory.h"
#include "proto_field_extraction/field_value_extractor/field_value_extractor_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcFieldExtraction {
namespace {

using Protobuf::field_extraction::FieldValueExtractorFactory;

} // namespace

absl::Status ExtractorImpl::init() {
  FieldValueExtractorFactory extractor_factory(type_finder_);
  for (const auto& it : field_extractions_.request_field_extractions()) {
    auto extractor = extractor_factory.Create(request_type_url_, it.first);
    if (!extractor.ok()) {
      return extractor.status();
    }

    per_field_extractors_.emplace(it.first, std::move(extractor.value()));
  }
  return absl::OkStatus();
}

absl::StatusOr<ExtractionResult>
ExtractorImpl::processRequest(Protobuf::field_extraction::MessageData& message) const {

  ExtractionResult result;
  for (const auto& it : per_field_extractors_) {
    auto extracted_values = it.second->Extract(message);
    if (!extracted_values.ok()) {
      return extracted_values.status();
    }

    ENVOY_LOG_MISC(debug, "extracted the following resource values from the {} field: {}", it.first,
                   std::accumulate(extracted_values.value().begin(), extracted_values.value().end(),
                                   std::string(),
                                   [](const std::string& lhs, const std::string& rhs) {
                                     return absl::StrFormat("%s, %s", lhs, rhs);
                                   }));
    result.push_back({it.first, std::move(extracted_values.value())});
  }

  return result;
}

} // namespace GrpcFieldExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
