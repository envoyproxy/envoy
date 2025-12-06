#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "source/common/grpc/common.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

absl::StatusOr<absl::string_view>
FieldChecker::resolveEnumName(absl::string_view value_str, const Protobuf::Field* field) const {
  int enum_number;
  if (!absl::SimpleAtoi(value_str, &enum_number)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Enum value '", value_str, "' is not a valid integer."));
  }

  // Extract Type Name from URL "type.googleapis.com/package.Name"
  absl::string_view type_name = Envoy::TypeUtil::typeUrlToDescriptorFullName(field->type_url());

  // Return the corresponding enum name.
  return filter_config_ptr_->getEnumName(type_name, enum_number);
}

std::string FieldChecker::constructFieldMask(const std::vector<std::string>& path,
                                             const Protobuf::Field* field) const {
  if (path.empty()) {
    return "";
  }

  // Translate the last segment of the `path` wherever required.
  absl::string_view last_segment = path.back();
  switch (field->kind()) {
  case Protobuf::Field::TYPE_ENUM: {
    if (auto name_or_status = resolveEnumName(last_segment, field);
        name_or_status.ok() && !name_or_status.value().empty()) {
      last_segment = name_or_status.value();
    } else {
      ENVOY_LOG(warn, "Enum translation skipped for value '{}': {}", last_segment,
                name_or_status.status().ToString());
    }
  } break;

  default:
    break;
  }

  // If path has only 1 segment, just return it (translated or original).
  if (path.size() == 1) {
    return std::string(last_segment);
  }

  // Join all segments except the last one, then append the (potentially translated) last segment.
  return absl::StrCat(absl::StrJoin(path.begin(), path.end() - 1, "."), ".", last_segment);
}

FieldCheckResults FieldChecker::CheckField(const std::vector<std::string>& path,
                                           const Protobuf::Field* field) const {
  const std::string field_mask = constructFieldMask(path, field);

  MatchTreeHttpMatchingDataSharedPtr match_tree;

  switch (scrubber_context_) {
  case ScrubberContext::kRequestScrubbing:
    match_tree = filter_config_ptr_->getRequestFieldMatcher(method_name_, field_mask);
    break;
  case ScrubberContext::kResponseScrubbing:
    match_tree = filter_config_ptr_->getResponseFieldMatcher(method_name_, field_mask);
    break;
  default:
    ENVOY_LOG(
        warn,
        "Error encountered while matching the field `{}`. This field would be preserved. Internal "
        "error details: Unsupported scrubber context enum value: `{}`. Supported values are: {{{}, "
        "{}}}.",
        field_mask, static_cast<int>(scrubber_context_),
        static_cast<int>(ScrubberContext::kRequestScrubbing),
        static_cast<int>(ScrubberContext::kResponseScrubbing));
    return FieldCheckResults::kInclude;
  }

  // If there's a match tree configured for the field, evaluate the match, convert the match result
  // to FieldCheckResults and return it.
  if (match_tree != nullptr) {
    absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(match_tree);
    return matchResultStatusToFieldCheckResult(match_result, field_mask);
  }

  // If there's no match tree configured for the field, check the field type to see if it needs to
  // traversed further. All non-primitive field types e.g., message, enums, maps, etc., if not
  // excluded above via match tree need to be traversed further. Returning `kPartial` makes sure
  // that the `proto_scrubber` library traverses the child fields of this field. Currently, only
  // message type is supported by FieldChecker. Support for other non-primitive types will be added
  // in the future.
  if (field->kind() == Protobuf::Field_Kind_TYPE_MESSAGE) {
    return FieldCheckResults::kPartial;
  }

  return FieldCheckResults::kInclude;
}

FieldCheckResults FieldChecker::matchResultStatusToFieldCheckResult(
    absl::StatusOr<Matcher::MatchResult>& match_result, absl::string_view field_mask) const {
  // Preserve the field (i.e., kInclude) if there's any error in evaluating the match.
  // This can happen in two cases:
  // 1. The match tree is corrupt.
  // 2. The required data to match is not present in the `matching_data_`.
  // Ideally both of these cases shouldn't happen as:
  // 1. The match tree is configured as part of filter config which is validated during filter
  // initialization itself.
  // 2. The field checker is created only after all the required data to match is received.
  // For now, it will emit an error log and preserve the field.
  if (!match_result.ok()) {
    ENVOY_LOG(warn,
              "Error encountered while matching the field `{}`. This field would be preserved. "
              "Error details: {}",
              field_mask, match_result.status().ToString());
    return FieldCheckResults::kInclude;
  }

  // Preserve the field (i.e., kInclude) if there's no match.
  if (match_result->isNoMatch()) {
    return FieldCheckResults::kInclude;
  }

  // Remove the field (i.e., kExclude) if there's a match and the matched action is
  // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`.
  if (match_result->action() != nullptr &&
      match_result->action()->typeUrl() ==
          "envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction") {
    return FieldCheckResults::kExclude;
  } else {
    // Preserve the field (i.e., kInclude) if there's a match and the matched action is not
    // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`.
    ENVOY_LOG(warn,
              "Field `{}` matched a rule, but the action type '{}' is not supported for "
              "field-level scrubbing. Field will be included.",
              field_mask, match_result->action() ? match_result->action()->typeUrl() : "null");
    return FieldCheckResults::kInclude;
  }
}

absl::StatusOr<Matcher::MatchResult>
FieldChecker::tryMatch(MatchTreeHttpMatchingDataSharedPtr match_tree) const {
  Matcher::MatchResult match_result = match_tree->match(matching_data_);
  if (!match_result.isComplete()) {
    return absl::InternalError("Matching couldn't complete due to insufficient data.");
  }

  return match_result;
}

FieldCheckResults FieldChecker::CheckType(const Protobuf::Type* type) const {
  if (type == nullptr) {
    return FieldCheckResults::kInclude;
  }

  const std::string& message_name = type->name();
  MatchTreeHttpMatchingDataSharedPtr message_matcher =
      filter_config_ptr_->getMessageMatcher(message_name);

  if (message_matcher != nullptr) {
    absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(message_matcher);

    if (!match_result.ok()) {
      ENVOY_LOG(warn,
                "Error encountered while matching message type `{}`: {}. Message will be included.",
                message_name, match_result.status().ToString());
      return FieldCheckResults::kInclude;
    }

    if (match_result->isMatch()) {
      // Use RemoveFieldAction to indicate that the entire message content should be scrubbed.
      if (match_result->action() != nullptr &&
          match_result->action()->typeUrl() ==
              "envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction") {
        ENVOY_LOG(debug, "Message type {} is excluded by message-level restriction.", message_name);
        return FieldCheckResults::kExclude; // Scrub the entire message content.
      } else {
        ENVOY_LOG(warn,
                  "Message type {} matched a rule, but the action type '{}' is not supported for "
                  "message-level scrubbing. Message will be included.",
                  message_name,
                  match_result->action() ? match_result->action()->typeUrl() : "null");
      }
    }
  }
  // No matching exclusion rule, so continue to process fields within the message.
  return FieldCheckResults::kInclude;
}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
