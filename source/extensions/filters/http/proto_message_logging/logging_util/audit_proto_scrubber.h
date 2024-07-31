#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "source/extensions/filters/http/proto_message_logging/logging_util/logging_util.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"
// #include "google/api/policy.proto.h"
// #include "google/protobuf/field_mask.proto.h"
// #include "google/protobuf/struct.proto.h"
#include "absl/strings/string_view.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "src/google/protobuf/util/converter/type_info.h"
// #include "proto_field_extraction/test_utils/utils.h"
// #include
// "proto_processing_lib/proto_scrubber/cloud_audit_log_field_checker.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {

// An implementation of ProtoScrubberInterface for Audit Logging
// using proto_processing_lib::proto_scrubber::ProtoScrubber.
class AuditProtoScrubber : public ProtoScrubberInterface {
 public:
  static std::unique_ptr<ProtoScrubberInterface> Create(
      proto_processing_lib::proto_scrubber::ScrubberContext scrubber_context,
      const ::google::protobuf::util::converter::TypeInfo* type_info,
      const google::protobuf::Type* message_type,
      const FieldPathToScrubType& field_policies);

  // Input message must be a message data.
  AuditMetadata ScrubMessage(
      const google::protobuf::field_extraction::MessageData& message)
      const override;

  const std::string& MessageType() const override {
    return message_type_->name();
  }

 private:
  // Initializes an instance of ProtoScrubber using FieldPolicies. All other
  // relevant info can be obtained from
  // ::google::protobuf::util::converter::TypeInfo.
  AuditProtoScrubber(
      proto_processing_lib::proto_scrubber::ScrubberContext scrubber_context,
      const ::google::protobuf::util::converter::TypeInfo* type_info,
      const google::protobuf::Type* message_type,
      const FieldPathToScrubType& field_policies);

  // Populate the target resource or the target resource callback in the audit
  // metadata.
  void GetTargetResourceOrTargetResourceCallback(
      const Protobuf::FieldMask& field_mask,
      const google::protobuf::field_extraction::MessageData& message,
      bool callback, AuditMetadata* audit_metadata) const;

  // Maybe populates the provided `result.resource_location` field with
  // extracted target resource location value.
  void MaybePopulateResourceLocation(
      absl::string_view resource_selector,
      const google::protobuf::field_extraction::MessageData& raw_message,
      AuditMetadata* result) const;

  const ::google::protobuf::util::converter::TypeInfo* type_info_;
  // std::unique_ptr<google::protobuf::field_extraction::testing::TypeHelper>
  //     type_helper_;
  const google::protobuf::Type* message_type_;
  // We use std::map instead of absl::flat_hash_map because of flat_hash_map's
  // rehash behavior.
  std::map<AuditDirective, google::protobuf::FieldMask> directives_mapping_;
  std::function<const google::protobuf::Type*(const std::string&)> type_finder_;
  std::unique_ptr<
      proto_processing_lib::proto_scrubber::CloudAuditLogFieldChecker>
      field_checker_;
  std::unique_ptr<proto_processing_lib::proto_scrubber::ProtoScrubber>
      scrubber_;
  // A field path for 'location_selector' associated with the field marked as
  // 'AUDIT_TARGET_RESOURCE', or 'AUDIT_TARGET_RESOURCE_CALLBACK' or empty value
  // if not available.
  std::string target_resource_location_selector_;
};

}  // namespace ProtoMessageLogging
}  // namespace HttpFilters
}  // namespace Extensions
}  // namespace Envoy
