#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "source/common/grpc/common.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

namespace {

// Internal state used to track position and results during path traversal.
// This struct is local to the source file and does not rely on private class members.
struct TraversalState {
  const Protobuf::Descriptor* current_desc;
  std::vector<std::string> normalized_path;
  bool is_map_entry;

  TraversalState(const Protobuf::Descriptor* root, size_t capacity)
      : current_desc(root), is_map_entry(false) {
    normalized_path.reserve(capacity);
  }
};

// Updates the descriptor pointer based on the current field.
// Returns true if traversal can continue (message type), false otherwise.
inline bool resolveNextMessageDescriptor(const Protobuf::FieldDescriptor* field,
                                         TraversalState& state) {
  if (!field || field->type() != Protobuf::FieldDescriptor::TYPE_MESSAGE) {
    state.current_desc = nullptr;
    return false;
  }
  state.current_desc = field->message_type();
  return true;
}

// Checks if the current field is a map and performs normalization (key -> "value") if needed.
// Updates the traversal index if the map key segment is consumed.
inline void normalizeMapEntryPath(const Protobuf::FieldDescriptor* field,
                                  const std::vector<std::string>& path, size_t& index,
                                  TraversalState& state) {
  // Check if we are at a Map field and have a subsequent segment (the key) to consume.
  if (field->is_map() && index + 1 < path.size()) {
    state.normalized_path.push_back("value");

    // Skip the actual key segment in the input path since we normalized it to "value".
    index++;

    // If we just consumed the last segment (the key), this path points to a map entry.
    if (index == path.size() - 1) {
      state.is_map_entry = true;
    }

    // Advance the descriptor to the Map Value's type (Field number 2 is always 'value').
    const auto* value_field = state.current_desc->FindFieldByNumber(2);
    if (value_field && value_field->type() == Protobuf::FieldDescriptor::TYPE_MESSAGE) {
      state.current_desc = value_field->message_type();
    } else {
      // Value is primitive or enum; we cannot traverse deeper.
      state.current_desc = nullptr;
    }
  }
}

// Processes a single path segment: updates normalized path and advances descriptor.
void normalizePathSegment(const std::vector<std::string>& path, size_t& index,
                          TraversalState& state) {
  const std::string& segment = path[index];
  state.is_map_entry = false;

  // If descriptor context is lost, just append the raw segment.
  if (!state.current_desc) {
    state.normalized_path.push_back(segment);
    return;
  }

  const Protobuf::FieldDescriptor* field = state.current_desc->FindFieldByName(segment);
  state.normalized_path.push_back(segment);

  // Attempt to advance to the nested message descriptor.
  if (!resolveNextMessageDescriptor(field, state)) {
    return;
  }

  // Handle specific logic for Protobuf Map fields.
  normalizeMapEntryPath(field, path, index, state);
}

} // namespace

FieldChecker::FieldChecker(const ScrubberContext scrubber_context,
                           const Envoy::StreamInfo::StreamInfo* stream_info,
                           OptRef<const Http::RequestHeaderMap> request_headers,
                           OptRef<const Http::ResponseHeaderMap> response_headers,
                           OptRef<const Http::RequestTrailerMap> request_trailers,
                           OptRef<const Http::ResponseTrailerMap> response_trailers,
                           const std::string& method_name,
                           const ProtoApiScrubberFilterConfig* filter_config)
    : scrubber_context_(scrubber_context), matching_data_(*stream_info), method_name_(method_name),
      filter_config_ptr_(filter_config), root_descriptor_(nullptr) {

  if (request_headers.has_value()) {
    matching_data_.onRequestHeaders(request_headers.ref());
  }
  if (response_headers.has_value()) {
    matching_data_.onResponseHeaders(response_headers.ref());
  }
  if (request_trailers.has_value()) {
    matching_data_.onRequestTrailers(request_trailers.ref());
  }
  if (response_trailers.has_value()) {
    matching_data_.onResponseTrailers(response_trailers.ref());
  }

  // Initialize root descriptor to support advanced path normalization.
  auto method_desc_or_error = filter_config_ptr_->getMethodDescriptor(method_name_);
  if (method_desc_or_error.ok()) {
    const auto* method_desc = method_desc_or_error.value();
    root_descriptor_ = (scrubber_context_ == ScrubberContext::kRequestScrubbing)
                           ? method_desc->input_type()
                           : method_desc->output_type();
  }
}

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

const FieldChecker::NormalizationResult&
FieldChecker::normalizePath(const std::vector<std::string>& path) const {
  // Fast path: Check if the result is already cached.
  if (auto it = path_cache_.find(path); it != path_cache_.end()) {
    return it->second;
  }

  NormalizationResult result;

  // Edge case: Empty path or missing root descriptor.
  if (path.empty() || !root_descriptor_) {
    result.mask = absl::StrJoin(path, ".");
    result.is_map_entry = false;
    return path_cache_.emplace(path, result).first->second;
  }

  // Traversal: Walk the descriptor tree to normalize the path.
  TraversalState state(root_descriptor_, path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    normalizePathSegment(path, i, state);
  }

  // Cache and return the result.
  result.mask = absl::StrJoin(state.normalized_path, ".");
  result.is_map_entry = state.is_map_entry;

  return path_cache_.emplace(path, result).first->second;
}

FieldCheckResults FieldChecker::CheckField(const std::vector<std::string>& path,
                                           const Protobuf::Field* field, const int /*field_depth*/,
                                           const Protobuf::Type* parent_type) const {
  // If the field is unknown (i.e., not present in the descriptor), it should be preserved.
  if (field == nullptr) {
    return FieldCheckResults::kInclude;
  }

  // If the field itself holds a message or enum, check if that type is globally restricted.
  if (field->kind() == Protobuf::Field::TYPE_MESSAGE ||
      field->kind() == Protobuf::Field::TYPE_ENUM) {
    absl::string_view type_name = Envoy::TypeUtil::typeUrlToDescriptorFullName(field->type_url());
    MatchTreeHttpMatchingDataSharedPtr type_matcher =
        filter_config_ptr_->getMessageMatcher(std::string(type_name));

    if (type_matcher != nullptr) {
      absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(type_matcher);
      // If the matcher says "Remove", we exclude this field entirely.
      if (matchResultStatusToFieldCheckResult(match_result, type_name) ==
          FieldCheckResults::kExclude) {
        return FieldCheckResults::kExclude;
      }
    }
  }

  // Recover the parent_type from the filter config if the caller didn't provide it
  // (e.g. ProtoScrubber library processing Any).
  const Protobuf::Type* type_context = parent_type;
  if (type_context == nullptr) {
    type_context = filter_config_ptr_->getParentType(field);
  }

  MatchTreeHttpMatchingDataSharedPtr match_tree = nullptr;

  // Try to find a specific rule for this Message Type. This handles cases where we are scrubbing
  // inside an `Any` field or a recursive message, where the path has been reset or is relative
  // to the parent message.
  if (type_context != nullptr) {
    match_tree = filter_config_ptr_->getMessageFieldMatcher(type_context->name(), field->name());
  }

  // If no message-type rule found, fall back to Method-Path based lookup.
  if (match_tree == nullptr) {
    const auto& norm = normalizePath(path);

    // Explicitly preserve Map Keys.
    // We identify if we are at a map key using the normalized path metadata.
    if (norm.is_map_entry && field->number() == 1) {
      return FieldCheckResults::kInclude;
    }

    // Optimized Mask Construction:
    // If the field is NOT an Enum, we can avoid creating a new std::string copy.
    // We use the cached string reference directly.
    const std::string* field_mask_ptr = &norm.mask;
    std::string modified_mask; // Storage for modified mask if needed (Enum case).

    if (field->kind() == Protobuf::Field::TYPE_ENUM) {
      // Enums require value translation (int -> name), so we must create a copy.
      modified_mask = norm.mask;
      absl::string_view last_segment = path.back();
      if (auto name_or_status = resolveEnumName(last_segment, field);
          name_or_status.ok() && !name_or_status.value().empty()) {
        size_t last_dot = modified_mask.find_last_of('.');
        if (last_dot != std::string::npos) {
          modified_mask.replace(last_dot + 1, std::string::npos, name_or_status.value());
        } else {
          modified_mask = std::string(name_or_status.value());
        }
      } else {
        ENVOY_LOG(warn, "Enum translation skipped for value '{}': {}", last_segment,
                  name_or_status.status().ToString());
      }
      field_mask_ptr = &modified_mask;
    }

    switch (scrubber_context_) {
    case ScrubberContext::kRequestScrubbing:
      match_tree = filter_config_ptr_->getRequestFieldMatcher(method_name_, *field_mask_ptr);
      break;
    case ScrubberContext::kResponseScrubbing:
      match_tree = filter_config_ptr_->getResponseFieldMatcher(method_name_, *field_mask_ptr);
      break;
    default:
      ENVOY_LOG(warn,
                "Error encountered while matching the field `{}`. This field would be preserved. "
                "Internal "
                "error details: Unsupported scrubber context enum value: `{}`. Supported values "
                "are: {{{}, "
                "{}}}.",
                *field_mask_ptr, static_cast<int>(scrubber_context_),
                static_cast<int>(ScrubberContext::kRequestScrubbing),
                static_cast<int>(ScrubberContext::kResponseScrubbing));
      return FieldCheckResults::kInclude;
    }
  }

  // If there's a match tree configured for the field, evaluate the match, convert the match result
  // to FieldCheckResults and return it.
  if (match_tree != nullptr) {
    absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(match_tree);
    return matchResultStatusToFieldCheckResult(match_result, field->name());
  }

  // If there's no match tree configured for the field, check the field type to see if it needs to
  // traversed further. All non-primitive field types e.g., message, enums, maps, etc., if not
  // excluded above via match tree need to be traversed further. Returning `kPartial` makes sure
  // that the `proto_scrubber` library traverses the child fields of this field. Currently, only
  // message type is supported by FieldChecker. Support for other non-primitive types will be added
  // in the future.
  //
  // Returning kPartial for ENUM is required to trigger value-level inspection (the library calls
  // CheckField again with the enum value in the path).
  if (field->kind() == Protobuf::Field_Kind_TYPE_MESSAGE ||
      field->kind() == Protobuf::Field_Kind_TYPE_ENUM) {
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
  auto it = match_result_cache_.find(getMatchTreeRawPtr(match_tree));
  if (it != match_result_cache_.end()) {
    return it->second;
  }

  Matcher::MatchResult match_result = match_tree->match(matching_data_);
  if (!match_result.isComplete()) {
    return absl::InternalError("Matching couldn't complete due to insufficient data.");
  }

  match_result_cache_.emplace(getMatchTreeRawPtr(match_tree), match_result);
  return match_result;
}

FieldCheckResults FieldChecker::CheckType(const Protobuf::Type* type) const {
  if (type == nullptr) {
    return FieldCheckResults::kPartial;
  }

  // Check if there is a message-level restriction for this specific type.
  // This handles scrubbing the entire payload of an Any field if the type matches.
  auto match_tree = filter_config_ptr_->getMessageMatcher(type->name());
  if (match_tree != nullptr) {
    absl::StatusOr<Matcher::MatchResult> match_result = tryMatch(match_tree);

    if (matchResultStatusToFieldCheckResult(match_result, type->name()) ==
        FieldCheckResults::kExclude) {
      return FieldCheckResults::kExclude;
    }
  }

  // Always return kPartial to force the ProtoScrubber to unpack and inspect the Any message.
  return FieldCheckResults::kPartial;
}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
