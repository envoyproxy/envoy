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
   * @param json_obj the parsed JSON object.
   * @param rule the rule to apply.
   * @return true if a value was successfully extracted and written.
   */
  bool applyRule(const Json::ObjectSharedPtr& json_obj, const Rule& rule);

  /**
   * Extract a value from JSON using the selector path.
   * @param json_obj the parsed JSON object.
   * @param path the selector path (sequence of keys).
   * @return the extracted value, or nullptr if not found.
   */
  absl::StatusOr<Json::ValueType> extractValueFromJson(const Json::ObjectSharedPtr& json_obj,
                                                       const std::vector<std::string>& path) const;

  /**
   * Write a value to dynamic metadata.
   * @param value the value to write.
   * @param descriptor the metadata descriptor specifying where to write.
   */
  void writeMetadata(const Json::ValueType& value, const MetadataDescriptor& descriptor);

  /**
   * Convert a JSON value to a Protobuf Value.
   * @param json_value the JSON value.
   * @param type the desired value type.
   * @return the Protobuf Value, or error status.
   */
  absl::StatusOr<Protobuf::Value> convertToProtobufValue(const Json::ValueType& json_value,
                                                         ValueType type) const;

  std::shared_ptr<FilterConfig> config_;
  bool should_process_{false};
  bool stop_processing_{false};
  std::string buffer_;
};

} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
