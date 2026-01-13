#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/stream_to_metadata/v3/stream_to_metadata.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {

/**
 * All stats for the Stream to Metadata filter. @see stats_macros.h
 */
#define ALL_STREAM_TO_METADATA_FILTER_STATS(COUNTER)                                               \
  COUNTER(success)                                                                                 \
  COUNTER(mismatched_content_type)                                                                 \
  COUNTER(no_data_field)                                                                           \
  COUNTER(invalid_json)                                                                            \
  COUNTER(selector_not_found)                                                                      \
  COUNTER(preserved_existing_metadata)                                                             \
  COUNTER(event_too_large)

/**
 * Wrapper struct for Stream to Metadata filter stats. @see stats_macros.h
 */
struct StreamToMetadataStats {
  ALL_STREAM_TO_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using ProtoRule = envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::Rule;
using MetadataDescriptor =
    envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::MetadataDescriptor;
using Selector =
    envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::Selector;
using ValueType =
    envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::ValueType;
using Format = envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata::Format;

/**
 * Data structure to store one rule with parsed selector path.
 */
struct Rule {
  Rule(const ProtoRule& rule);
  const ProtoRule rule_;
  std::vector<std::string> selector_path_;
};

using Rules = std::vector<Rule>;

/**
 * Configuration for the Stream to Metadata filter.
 */
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata& config,
      Stats::Scope& scope);

  StreamToMetadataStats& stats() { return stats_; }
  Format format() const { return format_; }
  const Rules& rules() const { return rules_; }
  bool isContentTypeAllowed(absl::string_view content_type) const;
  uint32_t maxEventSize() const { return max_event_size_; }

private:
  StreamToMetadataStats stats_;
  const Format format_;
  const Rules rules_;
  const absl::flat_hash_set<std::string> allowed_content_types_;
  const uint32_t max_event_size_;
};

/**
 * HTTP Stream to Metadata Filter.
 * Extracts values from streaming HTTP response bodies and writes them to dynamic metadata.
 * Currently supports parsing Server-Sent Events (SSE) format with JSON path selectors.
 */
class Filter : public Http::PassThroughEncoderFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(std::shared_ptr<FilterConfig> config);
  ~Filter() override = default;

  // Http::PassThroughEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

private:
  /**
   * Process the buffered data to find complete SSE events and extract metadata.
   * @param end_stream whether this is the last chunk of data.
   */
  void processBuffer(bool end_stream);

  /**
   * Process a single complete SSE event.
   * @param event the event content (without trailing blank line).
   * @return true if processing should stop (when stop_processing_on_match is true and a match was
   * found).
   */
  bool processSseEvent(absl::string_view event);

  /**
   * Apply a rule to extract a value from JSON and write to metadata.
   * Handles on_present (success), tracks on_missing (path not found) and on_error (errors).
   * on_missing and on_error are deferred until the end of stream.
   * @param json_obj the parsed JSON object.
   * @param rule_index the index of the rule in config_->rules().
   * @return true if on_present executed and stop_processing_on_match is true.
   */
  bool applyRule(const Json::ObjectSharedPtr& json_obj, size_t rule_index);

  /**
   * Write metadata for on_present case (value successfully extracted).
   * @param extracted_value the value extracted from the stream.
   * @param descriptors the metadata descriptors from on_present.
   */
  void writeOnPresent(const Json::ValueType& extracted_value,
                      const Protobuf::RepeatedPtrField<MetadataDescriptor>& descriptors);

  /**
   * Write metadata for on_missing case (selector path not found).
   * @param descriptors the metadata descriptors from on_missing.
   */
  void writeOnMissing(const Protobuf::RepeatedPtrField<MetadataDescriptor>& descriptors);

  /**
   * Write metadata for on_error case (JSON parse failure, no data field, etc).
   * @param descriptors the metadata descriptors from on_error.
   */
  void writeOnError(const Protobuf::RepeatedPtrField<MetadataDescriptor>& descriptors);

  /**
   * Finalize all rules: execute on_missing or on_error for rules where on_present never executed.
   * Called at end of stream or when processing_complete_ is set.
   */
  void finalizeRules();

  /**
   * Extract a value from JSON using the selector path.
   * @param json_obj the parsed JSON object.
   * @param path the selector path (sequence of keys).
   * @return the extracted value, or error status if not found.
   */
  absl::StatusOr<Json::ValueType> extractValueFromJson(const Json::ObjectSharedPtr& json_obj,
                                                       const std::vector<std::string>& path) const;

  /**
   * Write a value to dynamic metadata.
   * @param extracted_value optional extracted value from stream. If descriptor.value is not set,
   *        this will be used. If descriptor.value is set, descriptor.value takes precedence.
   * @param descriptor the metadata descriptor specifying where to write.
   */
  void writeMetadata(const absl::optional<Json::ValueType>& extracted_value,
                     const MetadataDescriptor& descriptor);

  /**
   * Convert a JSON value to a Protobuf Value.
   * @param json_value the JSON value.
   * @param type the desired value type.
   * @return the Protobuf Value, or error status.
   */
  absl::StatusOr<Protobuf::Value> convertToProtobufValue(const Json::ValueType& json_value,
                                                         ValueType type) const;

  // Per-rule state tracking for deferred on_missing/on_error
  struct RuleState {
    bool has_on_present_executed{false};
    bool has_error_occurred{false};
    bool has_missing_occurred{false};
  };

  std::shared_ptr<FilterConfig> config_;
  // Set to true if Content-Type header matches allowed types.
  bool content_type_matched_{false};
  // Set to true when a rule with stop_processing_on_match executes. Stops further processing.
  bool processing_complete_{false};
  std::string buffer_;
  // State tracking for each rule (indexed by rule position in config_->rules())
  std::vector<RuleState> rule_states_;
};

} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
