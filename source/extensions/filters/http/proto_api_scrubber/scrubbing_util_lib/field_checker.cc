#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

FieldCheckResults FieldChecker::CheckField(const std::vector<std::string>&,
                                           const Protobuf::Field* field) const {
  // Currently, this method only checks the top level request/response fields and hence, the field
  // name itself represents the field_mask.
  std::string field_mask = field->name();

  MatchTreeHttpMatchingDataSharedPtr match_tree =
      (scrubber_context_ == ScrubberContext::kRequestScrubbing)
          ? filter_config_ptr_->getRequestFieldMatcher(method_name_, field_mask)
          : filter_config_ptr_->getResponseFieldMatcher(method_name_, field_mask);

  // Preserve the field (i.e., kInclude) if there is no match tree configured for it.
  if (match_tree == nullptr) {
    return FieldCheckResults::kInclude;
  }

  absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(match_tree);

  // Preserve the field (i.e., kInclude) if there's any error in evaluating the match.
  // This can happen in two cases:
  // 1. The match tree is corrupt.
  // 2. The required data to match is not present in the `matching_data_ptr_`.
  // Ideally both of these cases shouldn't happen as:
  // 1. The match tree is configured as part of filter config which is validated during filter initialization itself.
  // 2. The field checker is created only after all the required data to match is received.
  // For now, it will emit an error log and preserve the field.
  if (!match_result.ok()) {
    ENVOY_LOG(error, "Matching failed for the field `{}`. This field would be preserved.", field_mask);
    return FieldCheckResults::kInclude;
  }

  // Preserve the field (i.e., kInclude) if there's no match.
  if (match_result->isNoMatch()) {
    return FieldCheckResults::kInclude;
  }

  // Remove the field (i.e., kExclude) if there's a match and the matched action is
  // `envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction`.
  if (match_result->action()->typeUrl() ==
      "envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction") {
    return FieldCheckResults::kExclude;
  }

  return FieldCheckResults::kInclude;
}

absl::StatusOr<Matcher::MatchResult>
FieldChecker::tryMatch(MatchTreeHttpMatchingDataSharedPtr match_tree) const {
  Matcher::MatchResult match_result = match_tree->match(*matching_data_ptr_);
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
