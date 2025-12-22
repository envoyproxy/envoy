#pragma once

#include <string>
#include <vector>

#include "source/common/common/logger.h"
#include "source/common/http/matching/data_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "absl/container/flat_hash_map.h"
#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"
#include "proto_processing_lib/proto_scrubber/proto_scrubber_enums.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

using proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using proto_processing_lib::proto_scrubber::FieldCheckResults;
using proto_processing_lib::proto_scrubber::FieldFilters;
using proto_processing_lib::proto_scrubber::ScrubberContext;

/**
 * FieldChecker class encapsulates the scrubbing logic of `ProtoApiScrubber` filter.
 * This `FieldChecker` would be integrated with `proto_processing_lib::proto_scrubber` library for
 * protobuf payload scrubbing. The `CheckField()` method declared in the parent class
 * `FieldCheckerInterface` and defined in this class is called by the
 * `proto_processing_lib::proto_scrubber` library for each field of the protobuf payload to decide
 * whether to preserve, remove or traverse it further.
 */
class FieldChecker : public FieldCheckerInterface, public Logger::Loggable<Logger::Id::filter> {
public:
  FieldChecker(const ScrubberContext scrubber_context,
               const Envoy::StreamInfo::StreamInfo* stream_info,
               OptRef<const Http::RequestHeaderMap> request_headers,
               OptRef<const Http::ResponseHeaderMap> response_headers,
               OptRef<const Http::RequestTrailerMap> request_trailers,
               OptRef<const Http::ResponseTrailerMap> response_trailers,
               const std::string& method_name, const ProtoApiScrubberFilterConfig* filter_config);

  // This type is neither copyable nor movable.
  FieldChecker(const FieldChecker&) = delete;
  FieldChecker& operator=(const FieldChecker&) = delete;
  ~FieldChecker() override {}

  // Make all the overloads from the base class visible here so the one explicit
  // override doesn't hide the other signatures.
  using FieldCheckerInterface::CheckField;

  FieldCheckResults CheckField(const std::vector<std::string>& path, const Protobuf::Field* field,
                               const int field_depth,
                               const Protobuf::Type* parent_type) const override;

  /**
   * Returns whether the `field` should be included (kInclude), excluded (kExclude)
   * or traversed further (kPartial).
   */
  FieldCheckResults CheckField(const std::vector<std::string>& path,
                               const Protobuf::Field* field) const override {
    return CheckField(path, field, 0, nullptr);
  }

  /**
   * Returns false as it currently doesn't support `google.protobuf.Any` type.
   */
  bool SupportAny() const override { return false; }

  /**
   * Returns whether the `type` should be included (kInclude), excluded (kExclude)
   * or traversed further (kPartial).
   */
  FieldCheckResults CheckType(const Protobuf::Type* type) const override;

  FieldFilters FilterName() const override { return FieldFilters::FieldMaskFilter; }

private:
  /**
   * Uses the `match_tree` to try to evaluate the match with the matching data.
   * Returns absl error if there's any issue while evaluating the match.
   * Otherwise, returns the match result.
   */
  absl::StatusOr<Matcher::MatchResult>
  tryMatch(MatchTreeHttpMatchingDataSharedPtr match_tree) const;

  FieldCheckResults
  matchResultStatusToFieldCheckResult(absl::StatusOr<Matcher::MatchResult>& match_result,
                                      absl::string_view field_mask) const;

  // Resolves the string name of an Enum value.
  absl::StatusOr<absl::string_view> resolveEnumName(absl::string_view value_str,
                                                    const Protobuf::Field* field) const;

  // Struct to hold normalization result and metadata.
  struct NormalizationResult {
    std::string mask;
    bool is_map_entry; // True if the path points directly to a Map Entry (key/value pair).
  };

  // Optimized helper to walk the type descriptor and normalize map keys in the path.
  const NormalizationResult& normalizePath(const std::vector<std::string>& path) const;

  ScrubberContext scrubber_context_;
  Http::Matching::HttpMatchingDataImpl matching_data_;
  std::string method_name_;
  const ProtoApiScrubberFilterConfig* filter_config_ptr_;

  const Protobuf::Descriptor* root_descriptor_;

  // Cache normalized results.
  mutable absl::flat_hash_map<std::vector<std::string>, NormalizationResult> path_cache_;
};

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
