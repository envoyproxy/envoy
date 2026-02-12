#pragma once

#include <string>
#include <vector>

#include "envoy/content_parser/config.h"
#include "envoy/content_parser/factory.h"
#include "envoy/content_parser/parser.h"
#include "envoy/extensions/filters/http/sse_to_metadata/v3/sse_to_metadata.pb.h"
#include "envoy/server/factory_context.h"
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
namespace SseToMetadata {

/**
 * All stats for the SSE to Metadata filter. @see stats_macros.h
 */
#define ALL_SSE_TO_METADATA_FILTER_STATS(COUNTER)                                                  \
  COUNTER(metadata_added)                                                                          \
  COUNTER(metadata_from_fallback)                                                                  \
  COUNTER(mismatched_content_type)                                                                 \
  COUNTER(no_data_field)                                                                           \
  COUNTER(parse_error)                                                                             \
  COUNTER(preserved_existing_metadata)                                                             \
  COUNTER(event_too_large)

/**
 * Wrapper struct for SSE to Metadata filter stats. @see stats_macros.h
 */
struct SseToMetadataStats {
  ALL_SSE_TO_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the SSE to Metadata filter.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata& config,
               Server::Configuration::ServerFactoryContext& context);

  const SseToMetadataStats& stats() const { return stats_; }
  ContentParser::ParserFactory& parserFactory() { return *parser_factory_; }
  uint32_t maxEventSize() const { return max_event_size_; }

private:
  static SseToMetadataStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return SseToMetadataStats{ALL_SSE_TO_METADATA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  ContentParser::ParserFactoryPtr parser_factory_;
  const SseToMetadataStats stats_;
  const uint32_t max_event_size_;
};

/**
 * HTTP SSE to Metadata Filter.
 * Extracts values from Server-Sent Events (SSE) HTTP response bodies and writes them to dynamic
 * metadata. Uses pluggable content parsers to extract values from SSE data fields.
 */
class Filter : public Http::PassThroughEncoderFilter, Logger::Loggable<Logger::Id::sse> {
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
   * @return true if processing should stop (all rules have reached their match limits).
   */
  bool processSseEvent(absl::string_view event);

  /**
   * Finalize rules at end of stream. Executes deferred actions for rules that haven't matched.
   */
  void finalizeRules();

  /**
   * Write a metadata action to dynamic metadata.
   * @return true if metadata was successfully written, false otherwise.
   */
  bool writeMetadata(const ContentParser::MetadataAction& action);

  const std::shared_ptr<FilterConfig> config_;
  // Parser instance for this stream
  ContentParser::ParserPtr parser_;
  // Set to true if Content-Type header matches allowed types.
  bool content_type_matched_{false};
  // Set to true when all rules have reached their match limits. Stops further processing.
  bool processing_complete_{false};
  Buffer::OwnedImpl buffer_;
};

} // namespace SseToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
