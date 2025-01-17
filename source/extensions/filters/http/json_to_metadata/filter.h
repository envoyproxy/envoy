#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/json_to_metadata/v3/json_to_metadata.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

/**
 * All stats for the Json to Metadata filter. @see stats_macros.h
 */
#define ALL_JSON_TO_METADATA_FILTER_STATS(COUNTER)                                                 \
  COUNTER(success)                                                                                 \
  COUNTER(mismatched_content_type)                                                                 \
  COUNTER(no_body)                                                                                 \
  COUNTER(invalid_json_body)

/**
 * Wrapper struct for Json to Metadata filter stats. @see stats_macros.h
 */
struct JsonToMetadataStats {
  ALL_JSON_TO_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using ProtoRule = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::Rule;
using KeyValuePair =
    envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::KeyValuePair;
using ValueType = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::ValueType;

/**
 * Data structure to store one rule.
 */
struct Rule {
  Rule(const ProtoRule& rule);
  const ProtoRule rule_;
  std::vector<std::string> keys_;
};

using Rules = std::vector<Rule>;

/**
 * Configuration for the Json to Metadata filter.
 */
class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config,
      Stats::Scope& scope, Regex::Engine& regex_engine);

  JsonToMetadataStats& rqstats() { return rqstats_; }
  JsonToMetadataStats& respstats() { return respstats_; }
  // True if we have rules for requests
  bool doRequest() const { return !request_rules_.empty(); }
  bool doResponse() const { return !response_rules_.empty(); }
  const Rules& requestRules() const { return request_rules_; }
  const Rules& responseRules() const { return response_rules_; }
  bool requestContentTypeAllowed(absl::string_view) const;
  bool responseContentTypeAllowed(absl::string_view) const;

private:
  using ProtobufRepeatedRule = Protobuf::RepeatedPtrField<ProtoRule>;
  Rules generateRules(const ProtobufRepeatedRule& proto_rule) const;
  JsonToMetadataStats rqstats_;
  JsonToMetadataStats respstats_;
  const Rules request_rules_;
  const Rules response_rules_;
  const absl::flat_hash_set<std::string> request_allow_content_types_;
  const absl::flat_hash_set<std::string> response_allow_content_types_;
  const bool request_allow_empty_content_type_;
  const bool response_allow_empty_content_type_;
  const Regex::CompiledMatcherPtr request_allow_content_types_regex_;
  const Regex::CompiledMatcherPtr response_allow_content_types_regex_;
};

const uint32_t MAX_PAYLOAD_VALUE_LEN = 8 * 1024;

/**
 * HTTP Json to Metadata Filter.
 */
class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(std::shared_ptr<FilterConfig> config) : config_(config){};

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

private:
  using StructMap = absl::flat_hash_map<std::string, ProtobufWkt::Struct>;
  // Handle on_missing case of the `rule` and store in `struct_map`.
  void handleOnMissing(const Rule& rule, StructMap& struct_map,
                       Http::StreamFilterCallbacks& filter_callback);
  // Handle on_present case of the `rule` and store in `struct_map`, which depends on
  // the value of `parent_node->key`.
  absl::Status handleOnPresent(Json::ObjectSharedPtr parent_node, const std::string& key,
                               const Rule& rule, StructMap& struct_map,
                               Http::StreamFilterCallbacks& filter_callback);

  // Process the case without body, i.e., on_missing is applied for all rules.
  void handleAllOnMissing(const Rules& rules, bool should_clear_route_cache,
                          Http::StreamFilterCallbacks& filter_callback,
                          bool& processing_finished_flag);
  // Process the case with error, i.e., on_error is applied for all rules.
  void handleAllOnError(const Rules& rules, bool should_clear_route_cache,
                        Http::StreamFilterCallbacks& filter_callback,
                        bool& processing_finished_flag);
  // Parse the body while we have the whole json.
  void processBody(const Buffer::Instance* body, const Rules& rules, bool should_clear_route_cache,
                   JsonToMetadataStats& stats, Http::StreamFilterCallbacks& filter_callback,
                   bool& processing_finished_flag);
  void processRequestBody();
  void processResponseBody();

  const std::string& decideNamespace(const std::string& nspace) const;
  bool addMetadata(const std::string& meta_namespace, const std::string& key,
                   ProtobufWkt::Value val, const bool preserve_existing_metadata_value,
                   StructMap& struct_map, Http::StreamFilterCallbacks& filter_callback);
  void applyKeyValue(const std::string& value, const KeyValuePair& keyval, StructMap& struct_map,
                     Http::StreamFilterCallbacks& filter_callback);
  void applyKeyValue(double value, const KeyValuePair& keyval, StructMap& struct_map,
                     Http::StreamFilterCallbacks& filter_callback);
  void applyKeyValue(ProtobufWkt::Value value, const KeyValuePair& keyval, StructMap& struct_map,
                     Http::StreamFilterCallbacks& filter_callback);
  void finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                               bool should_clear_route_cache, const StructMap& struct_map,
                               bool& processing_finished_flag);

  std::shared_ptr<FilterConfig> config_;
  bool request_processing_finished_{false};
  bool response_processing_finished_{false};
};

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
