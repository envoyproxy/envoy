#pragma once

#include <string>
#include <vector>

#include "envoy/content_parser/config.h"
#include "envoy/content_parser/factory.h"
#include "envoy/content_parser/parser.h"
#include "envoy/extensions/filters/http/aws_eventstream_parser/v3/aws_eventstream_parser.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/aws/eventstream/eventstream_parser.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamParser {

/**
 * All stats for the AWS EventStream Parser filter. @see stats_macros.h
 */
#define ALL_AWS_EVENTSTREAM_PARSER_FILTER_STATS(COUNTER)                                           \
  COUNTER(metadata_added)                                                                          \
  COUNTER(metadata_from_fallback)                                                                  \
  COUNTER(mismatched_content_type)                                                                 \
  COUNTER(empty_payload)                                                                           \
  COUNTER(parse_error)                                                                             \
  COUNTER(preserved_existing_metadata)                                                             \
  COUNTER(eventstream_error)                                                                       \
  COUNTER(type_conversion_error)

/**
 * Wrapper struct for AWS EventStream Parser filter stats. @see stats_macros.h
 */
struct AwsEventstreamParserStats {
  ALL_AWS_EVENTSTREAM_PARSER_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the AWS EventStream Parser filter.
 */
using ProtoConfig =
    envoy::extensions::filters::http::aws_eventstream_parser::v3::AwsEventstreamParser;

class FilterConfig {
public:
  FilterConfig(const ProtoConfig& config, Server::Configuration::ServerFactoryContext& context);

  static constexpr absl::string_view DefaultHeaderNamespace =
      "envoy.filters.http.aws_eventstream_parser";

  const AwsEventstreamParserStats& stats() const { return stats_; }
  ContentParser::ParserFactory& parserFactory() { return *parser_factory_; }
  const Protobuf::RepeatedPtrField<ProtoConfig::HeaderRule>& headerRules() const {
    return header_rules_;
  }

private:
  static AwsEventstreamParserStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return AwsEventstreamParserStats{
        ALL_AWS_EVENTSTREAM_PARSER_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  ContentParser::ParserFactoryPtr parser_factory_;
  const AwsEventstreamParserStats stats_;
  const Protobuf::RepeatedPtrField<ProtoConfig::HeaderRule> header_rules_;
};

/**
 * HTTP AWS EventStream Parser Filter.
 * Extracts values from AWS EventStream HTTP response bodies and writes them to dynamic
 * metadata. Uses pluggable content parsers to extract values from message payloads.
 */
class Filter : public Http::PassThroughEncoderFilter, Logger::Loggable<Logger::Id::http> {
public:
  Filter(std::shared_ptr<FilterConfig> config);
  ~Filter() override = default;

  // Http::PassThroughEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

private:
  /**
   * Process the buffered data to find complete EventStream messages and extract metadata.
   * @param end_stream whether this is the last chunk of data.
   */
  void processBuffer();

  /**
   * Process a single complete EventStream message.
   * @param payload the message payload bytes.
   * @return true if processing should stop (all rules have reached their match limits).
   */
  bool processMessage(absl::string_view payload);

  /**
   * Process EventStream headers from a single message against configured header rules.
   * @param headers the parsed EventStream message headers.
   */
  void
  processMessageHeaders(const std::vector<Extensions::Common::Aws::Eventstream::Header>& headers);

  /**
   * Finalize rules at end of stream. Executes deferred actions for rules that haven't matched.
   */
  void finalizeRules();

  /**
   * Add a metadata action to the pending batch. Validates the action and accumulates
   * into structs_by_namespace_ for later flushing.
   * @return true if metadata was successfully added to pending batch, false otherwise.
   */
  bool addMetadata(const ContentParser::MetadataAction& action);

  /**
   * Flush all pending metadata to dynamic metadata via setDynamicMetadata().
   * One call per namespace. Clears the pending batch.
   */
  void writeMetadata();

  /**
   * Convert an EventStream header value to a Protobuf::Value for metadata.
   */
  static Protobuf::Value
  headerValueToProtobufValue(const Extensions::Common::Aws::Eventstream::HeaderValue& header_value);

  using StructMap = absl::flat_hash_map<std::string, Protobuf::Struct>;

  const std::shared_ptr<FilterConfig> config_;
  // Parser instance for this stream
  ContentParser::ParserPtr parser_;
  // Set to true if Content-Type header matches allowed types.
  bool content_type_matched_{false};
  // Set to true when all rules have reached their match limits. Stops further processing.
  bool processing_complete_{false};
  Buffer::OwnedImpl buffer_;
  // Pending metadata writes, batched by namespace.
  StructMap structs_by_namespace_;

  /**
   * Check if all header rules with stop_processing_after_matches limits have been satisfied.
   * Returns true if there are no header rules or all limited rules have reached their limits.
   */
  bool allHeaderRulesSatisfied() const;

  // Per-header-rule state tracking.
  struct HeaderRuleState {
    bool ever_matched{false};
    uint32_t match_count{0};
  };
  std::vector<HeaderRuleState> header_rule_states_;
};

} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
