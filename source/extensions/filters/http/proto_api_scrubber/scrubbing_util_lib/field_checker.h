#pragma once

#include <string>
#include <vector>

#include "source/common/http/matching/data_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

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
class FieldChecker : public FieldCheckerInterface {
public:
  FieldChecker(const ScrubberContext scrubber_context,
               const Envoy::StreamInfo::StreamInfo* stream_info, const std::string method_name,
               const ProtoApiScrubberFilterConfig* filter_config) {
    stream_info_ptr_ = stream_info;
    scrubber_context_ = scrubber_context;
    method_name_ = method_name;
    filter_config_ptr_ = filter_config;

    matching_data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(*stream_info_ptr_);
  }

  // This type is neither copyable nor movable.
  FieldChecker(const FieldChecker&) = delete;
  FieldChecker& operator=(const FieldChecker&) = delete;
  ~FieldChecker() override {}

  /**
   * Returns whether the `field` should be included (kInclude), excluded (kExclude)
   * or traversed further (kPartial).
   * Currently, this method only checks the top level request/response fields. The logic for nested
   * fields will be added in the future.
   */
  FieldCheckResults CheckField(const std::vector<std::string>&,
                               const Protobuf::Field* field) const override;

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

  const Envoy::StreamInfo::StreamInfo* stream_info_ptr_;
  const ProtoApiScrubberFilterConfig* filter_config_ptr_;
  ScrubberContext scrubber_context_;
  std::string method_name_;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> matching_data_ptr_ = nullptr;
};

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
