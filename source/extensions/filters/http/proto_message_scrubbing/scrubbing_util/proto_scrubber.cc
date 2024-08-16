#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/proto_scrubber.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/proto_scrubber_interface.h"
#include "source/extensions/filters/http/proto_message_scrubbing/scrubbing_util/scrubbing_util.h"

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
namespace ProtoMessageScrubbing {

using ::Envoy::Protobuf::FieldMask;
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
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using ::proto_processing_lib::proto_scrubber::UnknownFieldChecker;

const google::protobuf::FieldMask&
ProtoScrubber::FindWithDefault(ScrubbedMessageDirective directive) {
  static const google::protobuf::FieldMask default_field_mask;

  auto it = directives_mapping_.find(directive);
  if (it != directives_mapping_.end()) {
    return it->second;
  } else {
    return default_field_mask;
  }
}

ProtoScrubber::ProtoScrubber(ScrubberContext scrubber_context, const TypeHelper* type_helper,
                             const Type* message_type, const FieldPathToScrubType& field_policies) {
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
    if (type.ok()) {
      result = *type;
    }
    return result;
  };

  for (const auto& directive : directives_mapping_) {
    LOG(INFO) << "Scrubbing Directive: " << std::to_string(static_cast<int>(directive.first))
              << ": " << directive.second.DebugString();
  }

  // Initialize proto scrubber that retains fields annotated with SCRUB and
  // SCRUB_REDACT. Fields that are SCRUB_REDACT will be redacted after
  // scrubbing.
  Protobuf::FieldMask scrubbed_message_field_mask;
  FieldMaskUtil::Union(FindWithDefault(ScrubbedMessageDirective::SCRUB),
                       FindWithDefault(ScrubbedMessageDirective::SCRUB_REDACT),
                       &scrubbed_message_field_mask);

  // Only create the scrubber if there are fields to retain.
  if (!scrubbed_message_field_mask.paths().empty()) {
    field_checker_ = std::make_unique<CloudAuditLogFieldChecker>(message_type_, type_finder_);

    if (!scrubbed_message_field_mask.paths().empty()) {
      absl::Status status = field_checker_->AddOrIntersectFieldPaths(std::vector<std::string>(
          scrubbed_message_field_mask.paths().begin(), scrubbed_message_field_mask.paths().end()));
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
ProtoScrubber::Create(ScrubberContext scrubber_context, const TypeHelper* type_helper,
                      const Type* message_type, const FieldPathToScrubType& field_policies) {
  return absl::WrapUnique(
      new ProtoScrubber(scrubber_context, type_helper, message_type, field_policies));
}

ScrubbedMessageMetadata
ProtoScrubber::ScrubMessage(const Protobuf::field_extraction::MessageData& raw_message) const {
  Protobuf::field_extraction::CordMessageData message_copy(raw_message.ToCord());

  ScrubbedMessageMetadata scrubbed_message_metadata;

  // Populate scrub message metadata before scrubbing message.
  for (const auto& directive : directives_mapping_) {
    const Protobuf::FieldMask& field_mask = directive.second;
    switch (directive.first) {
    case ScrubbedMessageDirective::SCRUB:

      GetTargetResourceOrTargetResourceCallback(field_mask, message_copy, /*callback=*/false,
                                                &scrubbed_message_metadata);

      GetTargetResourceOrTargetResourceCallback(field_mask, message_copy, /*callback=*/true,
                                                &scrubbed_message_metadata);
      break;
    default:
      // No need to handle SCRUB_REDACT, and method level directives.
      break;
    }
  }
  // If there are no fields to retain, no need to scrub and only populate @type
  // property.
  if (scrubber_ == nullptr) {
    (*scrubbed_message_metadata.scrubbed_message.mutable_fields())[kTypeProperty].set_string_value(
        google::protobuf::util::converter::GetFullTypeWithUrl(message_type_->name()));
    return scrubbed_message_metadata;
  }

  bool success = ScrubToStruct(scrubber_.get(), *message_type_, *type_helper_, &message_copy,
                               &scrubbed_message_metadata.scrubbed_message);

  if (!success) {
    LOG(ERROR) << "Failed to scrub message.";
  }

  // Handle redacted fields.
  auto redact_field_mask = directives_mapping_.find(ScrubbedMessageDirective::SCRUB_REDACT);
  if (redact_field_mask != directives_mapping_.end()) {
    // Convert the paths to be redacted into camel case first, since the
    // resulting proto struct keys are in camel case.
    std::vector<std::string> redact_paths_camel_case;
    for (const std::string& path : redact_field_mask->second.paths()) {
      redact_paths_camel_case.push_back(google::protobuf::util::converter::ToCamelCase(path));
    }
    RedactPaths(redact_paths_camel_case, &scrubbed_message_metadata.scrubbed_message);
  }
  return scrubbed_message_metadata;
}

void ProtoScrubber::GetTargetResourceOrTargetResourceCallback(
    const Protobuf::FieldMask& field_mask, const Protobuf::field_extraction::MessageData& message,
    bool callback, ScrubbedMessageMetadata* scrubbed_message_metadata) const {
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
    scrubbed_message_metadata->target_resource_callback.emplace(*status_or_target_resource);
  } else {
    scrubbed_message_metadata->target_resource.emplace(*status_or_target_resource);
  }
}

} // namespace ProtoMessageScrubbing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
