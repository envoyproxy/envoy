#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "absl/strings/str_join.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

FieldCheckResults FieldChecker::CheckField(const std::vector<std::string>& path,
                                           const Protobuf::Field* field) const {
  const std::string field_mask = absl::StrJoin(path, ".");

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
    absl::StatusOr<Matcher::MatchResult>& match_result, const std::string& field_mask) const {
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
              field_mask, match_result.status().message());
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

FieldCheckResults FieldChecker::CheckType(const Protobuf::Type*) const {
  return FieldCheckResults::kInclude;
}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
