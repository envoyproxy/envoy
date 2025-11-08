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
  const std::string field_mask = field->name();

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

  // Preserve the field (i.e., kInclude) if there is no match tree configured for it.
  if (match_tree == nullptr) {
    return FieldCheckResults::kInclude;
  }

  absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(match_tree);

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
  if (match_result->action()->typeUrl() ==
      "envoy.extensions.filters.http.proto_api_scrubber.v3.RemoveFieldAction") {
    return FieldCheckResults::kExclude;
  }

  return FieldCheckResults::kInclude;
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
