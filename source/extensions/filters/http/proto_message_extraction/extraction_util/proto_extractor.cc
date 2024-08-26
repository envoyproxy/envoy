#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/extraction_util.h"
#include "source/extensions/filters/http/proto_message_extraction/extraction_util/proto_extractor_interface.h"

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
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"
#include "proto_processing_lib/proto_scrubber/field_mask_path_checker.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"
#include "proto_processing_lib/proto_scrubber/unknown_field_checker.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoMessageExtraction {

using ::Envoy::Protobuf::FieldMask;
using ::Envoy::Protobuf::util::JsonParseOptions;
using ::Envoy::ProtobufUtil::FieldMaskUtil;
using ::Envoy::ProtobufWkt::Struct;
using ::Envoy::ProtobufWkt::Type;
using ::google::grpc::transcoding::TypeHelper;
using ::google::protobuf::util::converter::JsonObjectWriter;
using ::google::protobuf::util::converter::ProtoStreamObjectSource;
using ::google::protobuf::util::converter::TypeInfo;
using ::proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using ::proto_processing_lib::proto_scrubber::FieldMaskPathChecker;
using ::proto_processing_lib::proto_scrubber::ScrubberContext;
using ::proto_processing_lib::proto_scrubber::UnknownFieldChecker;

const google::protobuf::FieldMask&
ProtoExtractor::FindWithDefault(ExtractedMessageDirective directive) {
  static const google::protobuf::FieldMask default_field_mask;

  auto it = directives_mapping_.find(directive);
  if (it != directives_mapping_.end()) {
    return it->second;
  } else {
    return default_field_mask;
  }
}

ProtoExtractor::ProtoExtractor(ScrubberContext scrubber_context, const TypeHelper* type_helper,
                               const Type* message_type,
                               const FieldPathToExtractType& field_policies) {
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
    ENVOY_LOG_MISC(debug, "Extraction Directive: {}: {}", static_cast<int>(directive.first),
                   directive.second.DebugString());
  }

  // Initialize proto scrubber that retains fields annotated with EXTRACT and
  // EXTRACT_REDACT. Fields that are EXTRACT_REDACT will be redacted after
  // extraction.
  Protobuf::FieldMask extracted_message_field_mask;
  FieldMaskUtil::Union(FindWithDefault(ExtractedMessageDirective::EXTRACT),
                       FindWithDefault(ExtractedMessageDirective::EXTRACT_REDACT),
                       &extracted_message_field_mask);

  // Only create the scrubber if there are fields to retain.
  if (!extracted_message_field_mask.paths().empty()) {
    field_checker_ = std::make_unique<FieldMaskPathChecker>(message_type_, type_finder_);

    if (!extracted_message_field_mask.paths().empty()) {
      absl::Status status = field_checker_->AddOrIntersectFieldPaths(
          std::vector<std::string>(extracted_message_field_mask.paths().begin(),
                                   extracted_message_field_mask.paths().end()));
      if (!status.ok()) {
        ENVOY_LOG_MISC(debug, "Failed to create proto scrubber. Status: {}", status);
      }
    }
    scrubber_ = std::make_unique<proto_processing_lib::proto_scrubber::ProtoScrubber>(
        message_type_, type_finder_,
        std::vector<const FieldCheckerInterface*>{field_checker_.get(),
                                                  UnknownFieldChecker::GetDefault()},
        scrubber_context);
  }
}

std::unique_ptr<ProtoExtractorInterface>
ProtoExtractor::Create(ScrubberContext scrubber_context, const TypeHelper* type_helper,
                       const Type* message_type, const FieldPathToExtractType& field_policies) {
  return absl::WrapUnique(
      new ProtoExtractor(scrubber_context, type_helper, message_type, field_policies));
}

ExtractedMessageMetadata
ProtoExtractor::ExtractMessage(const Protobuf::field_extraction::MessageData& raw_message) const {
  Protobuf::field_extraction::CordMessageData message_copy(raw_message.ToCord());

  ExtractedMessageMetadata extracted_message_metadata;

  // Populate extracted message metadata before extracting message.
  for (const auto& directive : directives_mapping_) {
    const Protobuf::FieldMask& field_mask = directive.second;
    switch (directive.first) {
    case ExtractedMessageDirective::EXTRACT:

      GetTargetResourceOrTargetResourceCallback(field_mask, message_copy, /*callback=*/false,
                                                &extracted_message_metadata);

      GetTargetResourceOrTargetResourceCallback(field_mask, message_copy, /*callback=*/true,
                                                &extracted_message_metadata);
      break;
    default:
      // No need to handle EXTRACT_REDACT, and method level directives.
      break;
    }
  }

  // If there are no fields to retain, no need to scrub and only populate @type
  // property.
  if (scrubber_ == nullptr) {
    (*extracted_message_metadata.extracted_message.mutable_fields())[kTypeProperty]
        .set_string_value(
            google::protobuf::util::converter::GetFullTypeWithUrl(message_type_->name()));
    return extracted_message_metadata;
  }

  bool success = ScrubToStruct(scrubber_.get(), *message_type_, *type_helper_, &message_copy,
                               &extracted_message_metadata.extracted_message);

  if (!success) {
    ENVOY_LOG_MISC(debug, "Failed to extract message.");
  }

  // Handle redacted fields.
  auto redact_field_mask = directives_mapping_.find(ExtractedMessageDirective::EXTRACT_REDACT);
  if (redact_field_mask != directives_mapping_.end()) {
    // Convert the paths to be redacted into camel case first, since the
    // resulting proto struct keys are in camel case.
    std::vector<std::string> redact_paths_camel_case;
    for (const std::string& path : redact_field_mask->second.paths()) {
      redact_paths_camel_case.push_back(google::protobuf::util::converter::ToCamelCase(path));
    }
    RedactPaths(redact_paths_camel_case, &extracted_message_metadata.extracted_message);
  }
  return extracted_message_metadata;
}

void ProtoExtractor::GetTargetResourceOrTargetResourceCallback(
    const Protobuf::FieldMask& field_mask, const Protobuf::field_extraction::MessageData& message,
    bool callback, ExtractedMessageMetadata* extracted_message_metadata) const {
  // There should be only one target resource; this is checked at config
  // compile time.
  if (field_mask.paths().empty()) {
    return;
  }

  // The extraction can success but the string can be empty.
  absl::StatusOr<std::string> status_or_target_resource =
      ExtractStringFieldValue(*message_type_, type_finder_, field_mask.paths(0), message);
  if (!status_or_target_resource.ok()) {
    ENVOY_LOG_MISC(debug, "Unable to extract target resource: {}",
                   status_or_target_resource.status());
    return;
  }

  // Only set up the target resource callback if there is a non empty extracted
  // value.
  if (callback && !status_or_target_resource.value().empty()) {
    extracted_message_metadata->target_resource_callback.emplace(*status_or_target_resource);
  } else {
    extracted_message_metadata->target_resource.emplace(*status_or_target_resource);
  }
}

} // namespace ProtoMessageExtraction
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
