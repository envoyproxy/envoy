#include "source/extensions/filters/http/proto_message_logging/logging_util/audit_proto_scrubber.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/proto_message_logging/logging_util/logging_util.h"
#include "source/extensions/filters/http/proto_message_logging/logging_util/proto_scrubber_interface.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/util/converter/json_objectwriter.h"
#include "google/protobuf/util/converter/protostream_objectsource.h"
#include "google/protobuf/util/converter/utility.h"
#include "grpc_transcoding/type_helper.h"
#include "proto_field_extraction/message_data/cord_message_data.h"
#include "proto_field_extraction/message_data/message_data.h"
#include "proto_processing_lib/proto_scrubber/cloud_audit_log_field_checker.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"
#include "proto_processing_lib/proto_scrubber/unknown_field_checker.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageLogging {
namespace {

using ::Envoy::Protobuf::FieldMask;
using ::Envoy::Protobuf::io::CodedOutputStream;
using ::Envoy::Protobuf::io::CordOutputStream;
using ::Envoy::Protobuf::util::JsonParseOptions;
using ::Envoy::ProtobufUtil::FieldMaskUtil;
using ::Envoy::ProtobufWkt::Struct;
using ::Envoy::ProtobufWkt::Type;
using ::google::grpc::transcoding::TypeHelper;
using ::google::protobuf::util::converter::JsonObjectWriter;
using ::google::protobuf::util::converter::ProtoStreamObjectSource;
using ::google::protobuf::util::converter::TypeInfo;
using ::proto_processing_lib::proto_scrubber::CloudAuditLogFieldChecker;
using ::proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using ::proto_processing_lib::proto_scrubber::ProtoScrubber;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using ::proto_processing_lib::proto_scrubber::UnknownFieldChecker;

absl::Status ConvertToStruct(const Protobuf::field_extraction::MessageData& message,
                             const Envoy::ProtobufWkt::Type& type, const TypeHelper& type_helper,
                             Struct* message_struct) {
  // Convert from message data to JSON using absl::Cord.
  auto in_stream = message.CreateCodedInputStreamWrapper();
  ProtoStreamObjectSource os(&in_stream->Get(), type_helper.Resolver(), type);
  os.set_max_recursion_depth(kProtoTranslationMaxRecursionDepth);

  CordOutputStream cord_out_stream;
  CodedOutputStream out_stream(&cord_out_stream);
  JsonObjectWriter json_object_writer("", &out_stream);

  if (!os.WriteTo(&json_object_writer).ok()) {
    return absl::InternalError("Failed to write to JSON object writer.");
  }
  out_stream.Trim();

  // Convert from JSON (in absl::Cord) to Struct.
  JsonParseOptions options;
  auto status = Protobuf::util::JsonStringToMessage(cord_out_stream.Consume().Flatten(),
                                                    message_struct, options);
  if (!status.ok()) {
    return absl::InternalError(
        absl::StrCat("Failed to parse Struct from formatted JSON of '", type.name(), "' message."));
  }

  (*message_struct->mutable_fields())[kTypeProperty].set_string_value(
      google::protobuf::util::converter::GetFullTypeWithUrl(type.name()));
  return absl::OkStatus();
}

bool ScrubToStruct(const ProtoScrubber* scrubber, const Envoy::ProtobufWkt::Type& type,
                   const TypeHelper& type_helper, Protobuf::field_extraction::MessageData* message,
                   Envoy::ProtobufWkt::Struct* message_struct) {
  message_struct->Clear();

  // When scrubber or message is nullptr, it indicates that there's nothing to
  // scrub and the whole message should be filtered.
  if (scrubber == nullptr || message == nullptr) {
    return false;
  }

  // Scrub the message.
  absl::Status status = scrubber->Scrub(message);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to scrub " << type.name()
                 << "proto for cloud audit logging: " << status.ToString();
    return false;
  }

  // Convert the scrubbed message to proto.
  status = ConvertToStruct(*message, type, type_helper, message_struct);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to convert " << type.name()
                 << " proto to google.protobuf.Struct for cloud "
                    "audit logging: "
                 << status.ToString();
    return false;
  }

  return !IsEmptyStruct(*message_struct);
}
} // namespace

const google::protobuf::FieldMask& AuditProtoScrubber::FindWithDefault(AuditDirective directive) {
  static const google::protobuf::FieldMask default_field_mask;

  auto it = directives_mapping_.find(directive);
  if (it != directives_mapping_.end()) {
    return it->second;
  } else {
    return default_field_mask;
  }
}

AuditProtoScrubber::AuditProtoScrubber(ScrubberContext scrubber_context,
                                       const TypeHelper* type_helper, const Type* message_type,
                                       const FieldPathToScrubType& field_policies) {
  type_helper_ = type_helper;
  message_type_ = message_type;
  for (const auto& field_policy : field_policies) {
    for (const auto& directive : field_policy.second) {
      directives_mapping_[directive].add_paths(field_policy.first);
    }
  }

  // Initialize type finder.
  type_finder_ = [&](const std::string& type_url) {
    const Type* result = nullptr;
    absl::StatusOr<const Type*> type = type_helper_->Info()->GetTypeByTypeUrl(type_url);
    if (!type.ok()) {
      LOG(WARNING) << "Failed to find Type for type url: " << type_url;
    } else {
      result = *type;
    }
    return result;
  };

  for (const auto& directive : directives_mapping_) {
    LOG(INFO) << "Audit Directive: " << std::to_string(static_cast<int>(directive.first)) << ": "
              << directive.second.DebugString();
  }

  // Initialize proto scrubber that retains fields annotated with AUDIT and
  // AUDIT_REDACT. Fields that are AUDIT_REDACT will be redacted after
  // scrubbing.
  Protobuf::FieldMask audit_field_mask;
  FieldMaskUtil::Union(FindWithDefault(AuditDirective::AUDIT),
                       FindWithDefault(AuditDirective::AUDIT_REDACT), &audit_field_mask);

  // Only create the scrubber if there are fields to retain.
  if (!audit_field_mask.paths().empty()) {
    field_checker_ = std::make_unique<CloudAuditLogFieldChecker>(message_type_, type_finder_);

    if (!audit_field_mask.paths().empty()) {
      absl::Status status = field_checker_->AddOrIntersectFieldPaths(std::vector<std::string>(
          audit_field_mask.paths().begin(), audit_field_mask.paths().end()));
      if (!status.ok()) {
        LOG(WARNING) << "Failed to create proto scrubber for message '" << message_type_->name()
                     << "' for audit logging: " << status;
      }
    }
    scrubber_ = std::make_unique<proto_processing_lib::proto_scrubber::ProtoScrubber>(
        message_type_, type_finder_,
        std::vector<const FieldCheckerInterface*>{field_checker_.get(),
                                                  UnknownFieldChecker::GetDefault()},
        scrubber_context);
  }
}

std::unique_ptr<ProtoScrubberInterface>
AuditProtoScrubber::Create(ScrubberContext scrubber_context, const TypeHelper* type_helper,
                           const Type* message_type, const FieldPathToScrubType& field_policies) {
  return absl::WrapUnique(
      new AuditProtoScrubber(scrubber_context, type_helper, message_type, field_policies));
}

AuditMetadata
AuditProtoScrubber::ScrubMessage(const Protobuf::field_extraction::MessageData& raw_message) const {
  Protobuf::field_extraction::CordMessageData message_copy(raw_message.ToCord());

  AuditMetadata audit_metadata;

  // Populate audit metadata before scrubbing message.
  for (const auto& directive : directives_mapping_) {
    const Protobuf::FieldMask& field_mask = directive.second;
    switch (directive.first) {
    case AuditDirective::AUDIT:
      // audit_metadata.num_response_items.emplace(ExtractRepeatedFieldSize(
      //     message_type_, type_finder_, &field_mask, message_copy));

      GetTargetResourceOrTargetResourceCallback(field_mask, message_copy, /*callback=*/false,
                                                &audit_metadata);

      GetTargetResourceOrTargetResourceCallback(field_mask, message_copy, /*callback=*/true,
                                                &audit_metadata);
      break;
    default:
      // No need to handle AUDIT_REDACT, and method level directives.
      break;
    }
  }
  // If there are no fields to retain, no need to scrub and only populate @type
  // property.
  if (scrubber_ == nullptr) {
    (*audit_metadata.scrubbed_message.mutable_fields())[kTypeProperty].set_string_value(
        google::protobuf::util::converter::GetFullTypeWithUrl(message_type_->name()));
    return audit_metadata;
  }

  bool success = ScrubToStruct(scrubber_.get(), *message_type_, *type_helper_, &message_copy,
                               &audit_metadata.scrubbed_message);

  if (!success) {
    LOG(ERROR) << "Failed to scrub message.";
  }

  // Handle redacted fields.
  auto redact_field_mask = directives_mapping_.find(AuditDirective::AUDIT_REDACT);
  if (redact_field_mask != directives_mapping_.end()) {
    // Convert the paths to be redacted into camel case first, since the
    // resulting proto struct keys are in camel case.
    std::vector<std::string> redact_paths_camel_case;
    for (const std::string& path : redact_field_mask->second.paths()) {
      redact_paths_camel_case.push_back(google::protobuf::util::converter::ToCamelCase(path));
    }
    RedactPaths(redact_paths_camel_case, &audit_metadata.scrubbed_message);
  }
  return audit_metadata;
}

void AuditProtoScrubber::GetTargetResourceOrTargetResourceCallback(
    const Protobuf::FieldMask& field_mask, const Protobuf::field_extraction::MessageData& message,
    bool callback, AuditMetadata* audit_metadata) const {
  // There should be only one target resource; this is checked at config
  // compile time.
  if (field_mask.paths().empty()) {
    return;
  }

  // The extraction can success but the string can be empty.
  absl::StatusOr<std::string> status_or_target_resource =
      ExtractStringFieldValue(*message_type_, type_finder_, field_mask.paths(0), message);
  if (!status_or_target_resource.ok()) {
    LOG(ERROR) << "Unable to extract target resource: " << status_or_target_resource.status();
    return;
  }

  // Only set up the target resource callback if there is a non empty extracted
  // value.
  if (callback && !status_or_target_resource.value().empty()) {
    audit_metadata->target_resource_callback.emplace(*status_or_target_resource);
  } else {
    audit_metadata->target_resource.emplace(*status_or_target_resource);
  }

  MaybePopulateResourceLocation(field_mask.paths(0), message, audit_metadata);
}

void AuditProtoScrubber::MaybePopulateResourceLocation(
    absl::string_view resource_selector, const Protobuf::field_extraction::MessageData& raw_message,
    AuditMetadata* result) const {
  if (target_resource_location_selector_.empty()) {
    return;
  }

  absl::StatusOr<std::string> extracted_resource_location = ExtractStringFieldValue(
      *message_type_, type_finder_, target_resource_location_selector_, raw_message);

  if (!extracted_resource_location.ok()) {
    LOG(ERROR) << "Unable to extract resource location: " << extracted_resource_location.status();
  } else if (target_resource_location_selector_ == resource_selector) {
    // Resource location is in the same field as the resource name - need to
    // extract it.
    absl::string_view location_id = ExtractLocationIdFromResourceName(*extracted_resource_location);
    if (!location_id.empty()) {
      result->resource_location = location_id;
    }
  } else {
    result->resource_location = *extracted_resource_location;
  }
}

} // namespace ProtoMessageLogging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
