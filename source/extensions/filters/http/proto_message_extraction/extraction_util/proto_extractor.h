#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "source/extensions/filters/http/proto_message_extraction/extraction_util/extraction_util.h"
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor_interface.h"

#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/field_mask_path_checker.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

// An implementation of ProtoExtractorInterface for extraction
// using proto_processing_lib::proto_scrubber::ProtoScrubber.
class ProtoExtractor : public ProtoExtractorInterface {
public:
  static std::unique_ptr<ProtoExtractorInterface>
  Create(proto_processing_lib::proto_scrubber::ScrubberContext scrubber_context,
         const google::grpc::transcoding::TypeHelper* type_helper,
         const ::Envoy::ProtobufWkt::Type* message_type,
         const FieldPathToExtractType& field_policies);

  // Input message must be a message data.
  ExtractedMessageMetadata
  ExtractMessage(const Protobuf::field_extraction::MessageData& message) const override;

private:
  // Initializes an instance of ProtoExtractor using FieldPolicies.
  ProtoExtractor(proto_processing_lib::proto_scrubber::ScrubberContext scrubber_context,
                 const google::grpc::transcoding::TypeHelper* type_helper,
                 const ::Envoy::ProtobufWkt::Type* message_type,
                 const FieldPathToExtractType& field_policies);

  // Populate the target resource or the target resource callback in the extracted message
  // metadata.
  void GetTargetResourceOrTargetResourceCallback(
      const Protobuf::FieldMask& field_mask, const Protobuf::field_extraction::MessageData& message,
      bool callback, ExtractedMessageMetadata* extracted_message_metadata) const;

  // Function to get the value associated with a key
  const ProtobufWkt::FieldMask& FindWithDefault(ExtractedMessageDirective directive);

  const google::grpc::transcoding::TypeHelper* type_helper_;
  const ::Envoy::ProtobufWkt::Type* message_type_;
  // We use std::map instead of absl::flat_hash_map because of flat_hash_map's
  // rehash behavior.
  std::map<ExtractedMessageDirective, ProtobufWkt::FieldMask> directives_mapping_;
  std::function<const ::Envoy::ProtobufWkt::Type*(const std::string&)> type_finder_;
  std::unique_ptr<proto_processing_lib::proto_scrubber::FieldMaskPathChecker> field_checker_;
  std::unique_ptr<proto_processing_lib::proto_scrubber::ProtoScrubber> scrubber_;
  // A field path for 'location_selector' associated with the field marked as
  // 'EXTRACT_TARGET_RESOURCE', or 'EXTRACT_TARGET_RESOURCE_CALLBACK' or empty value
  // if not available.
  std::string target_resource_location_selector_;
};

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
