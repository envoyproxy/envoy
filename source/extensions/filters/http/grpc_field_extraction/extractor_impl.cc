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

// Extracts field values given a specific field mask.
absl::StatusOr<std::vector<std::string>>
ExtractFromPath(absl::string_view type_url, absl::string_view field_mask,
                google::protobuf::field_extraction::MessageData& message,
                FieldValueExtractorFactory& extractor_factory) {
  auto extractor = extractor_factory.Create(type_url, field_mask);
  if (!extractor.ok()) {
    return extractor.status();
  }

  return (*extractor)->Extract(message);
}

} // namespace

absl::Status
ExtractorImpl::ProcessRequest(google::protobuf::field_extraction::MessageData& message) {
  // Only process the first message
  if (message_count_++ > 0) {
    return absl::OkStatus();
  }

  FieldValueExtractorFactory extractor_factory(type_finder_);

  for (const auto& it : field_extractions_.request_field_extractions()) {
    auto resource = ExtractRequestField(extractor_factory, it.first, message);
    if (!resource.ok()) {
      return resource.status();
    }
    result_.req_fields.push_back(*resource);
  }

  return absl::OkStatus();
}

absl::StatusOr<RequestField>
ExtractorImpl::ExtractRequestField(FieldValueExtractorFactory& extractor_factory,
                                   absl::string_view field_path,
                                   google::protobuf::field_extraction::MessageData& message) const {
  // Extract the resource values from proto.
  RequestField request_field_values{
      field_path,
      /*values=*/{},
  };
  auto extracted_values =
      ExtractFromPath(request_type_url_, field_path, message, extractor_factory);
  if (!extracted_values.ok()) {
    return extracted_values.status();
  }

  request_field_values.values = std::move(*extracted_values);
  ENVOY_LOG_MISC(
      info, "Extracted the following resource values from the {} field: {}", request_type_url_,
      std::accumulate(request_field_values.values.begin(), request_field_values.values.end(),
                      std::string(),
                      [](const std::string& lhs, const std::string& rhs) { return lhs + rhs; }));

  return request_field_values;
}

} // namespace Envoy::Extensions::HttpFilters::GrpcFieldExtraction
