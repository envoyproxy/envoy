#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/proto_scrubber_interface.h"
#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/scrubbing_util.h"

#include "absl/strings/string_view.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/cloud_audit_log_field_checker.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageScrubbing {

// An implementation of ProtoScrubberInterface for scrubbing
// using proto_processing_lib::proto_scrubber::ProtoScrubber.
class ProtoScrubber : public ProtoScrubberInterface {
public:
  static std::unique_ptr<ProtoScrubberInterface>
  Create(proto_processing_lib::proto_scrubber::ScrubberContext scrubber_context,
         const google::grpc::transcoding::TypeHelper* type_helper,
         const ::Envoy::ProtobufWkt::Type* message_type,
         const FieldPathToScrubType& field_policies);

  // Input message must be a message data.
  ScrubbedMessageMetadata
  ScrubMessage(const Protobuf::field_extraction::MessageData& message) const override;

private:
  // Initializes an instance of ProtoScrubber using FieldPolicies.
  ProtoScrubber(proto_processing_lib::proto_scrubber::ScrubberContext scrubber_context,
                const google::grpc::transcoding::TypeHelper* type_helper,
                const ::Envoy::ProtobufWkt::Type* message_type,
                const FieldPathToScrubType& field_policies);

  // Populate the target resource or the target resource callback in the scrub message
  // metadata.
  void GetTargetResourceOrTargetResourceCallback(
      const Protobuf::FieldMask& field_mask, const Protobuf::field_extraction::MessageData& message,
      bool callback, ScrubbedMessageMetadata* scrubbed_message_metadata) const;

  // Function to get the value associated with a key
  const ProtobufWkt::FieldMask& FindWithDefault(ScrubbedMessageDirective directive);

  const google::grpc::transcoding::TypeHelper* type_helper_;
  const ::Envoy::ProtobufWkt::Type* message_type_;
  // We use std::map instead of absl::flat_hash_map because of flat_hash_map's
  // rehash behavior.
  std::map<ScrubbedMessageDirective, ProtobufWkt::FieldMask> directives_mapping_;
  std::function<const ::Envoy::ProtobufWkt::Type*(const std::string&)> type_finder_;
  std::unique_ptr<proto_processing_lib::proto_scrubber::CloudAuditLogFieldChecker> field_checker_;
  std::unique_ptr<proto_processing_lib::proto_scrubber::ProtoScrubber> scrubber_;
  // A field path for 'location_selector' associated with the field marked as
  // 'SCRUB_TARGET_RESOURCE', or 'SCRUB_TARGET_RESOURCE_CALLBACK' or empty value
  // if not available.
  std::string target_resource_location_selector_;
};

} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
