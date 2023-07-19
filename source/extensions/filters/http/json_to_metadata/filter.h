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
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

// clang-format off
#define ALL_JSON_TO_METADATA_FILTER_STATS(COUNTER)                \
  COUNTER(rq_success)                                             \
  COUNTER(rq_mismatched_content_type)                             \
  COUNTER(rq_no_body)                                             \
  COUNTER(rq_too_large_body)                                      \
  COUNTER(rq_invalid_json_body)
// clang-format on

struct JsonToMetadataStats {
  ALL_JSON_TO_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using ProtoRule = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::Rule;
using KeyValuePair =
    envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::KeyValuePair;
using ValueType = envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata::ValueType;

struct Rule {
  Rule(const ProtoRule& rule);
  const ProtoRule rule_;
  std::vector<std::string> keys_;
};

using Rules = std::vector<Rule>;

class FilterConfig {
public:
  FilterConfig(
      const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config,
      Stats::Scope& scope);
  ~FilterConfig() = default;

  JsonToMetadataStats& stats() { return stats_; }
  bool doRequest() const { return !request_rules_.empty(); }
  const Rules& requestRules() const { return request_rules_; }
  uint32_t requestBufferLimitBytes() const { return request_buffer_limit_bytes_; }
  bool requestContentTypeAllowed(absl::string_view) const;

private:
  using ProtobufRepeatedRule = Protobuf::RepeatedPtrField<ProtoRule>;
  Rules generateRequestRules(
      const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config);
  absl::flat_hash_set<std::string> generateRequestAllowContentTypes(
      const envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata& proto_config);
  JsonToMetadataStats stats_;
  const uint32_t request_buffer_limit_bytes_;
  const Rules request_rules_;
  const absl::flat_hash_set<std::string> request_allow_content_types_;
  const bool request_allow_empty_content_type_;
};

const uint32_t MAX_PAYLOAD_VALUE_LEN = 8 * 1024;

class Filter : public Http::PassThroughFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(std::shared_ptr<FilterConfig> config) : config_(config){};
  ~Filter() override;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;

private:
  using StructMap = absl::flat_hash_map<std::string, ProtobufWkt::Struct>;
  void handleOnMissing(const Rule& rule, StructMap& struct_map);
  absl::Status handleOnPresent(Json::ObjectSharedPtr parent_node, const std::string& key,
                               const Rule& rule, StructMap& struct_map);

  void handleAllOnMissing(const Rules& rules, bool& reported_flag);
  void handleAllOnError(const Rules& rules, bool& reported_flag);
  void processBody(const Buffer::Instance* body, const Rules& rules, bool& reported_flag,
                   Stats::Counter& success, Stats::Counter& no_body, Stats::Counter& non_json);
  void processRequestBody();

  const std::string& decideNamespace(const std::string& nspace) const;
  bool addMetadata(const std::string& meta_namespace, const std::string& key,
                   ProtobufWkt::Value val, const bool preserve_existing_metadata_value,
                   StructMap& struct_map);
  void applyKeyValue(const std::string& value, const KeyValuePair& keyval, StructMap& struct_map);
  void applyKeyValue(double value, const KeyValuePair& keyval, StructMap& struct_map);
  void applyKeyValue(ProtobufWkt::Value value, const KeyValuePair& keyval, StructMap& struct_map);
  void finalizeDynamicMetadata(Http::StreamFilterCallbacks& filter_callback,
                               const StructMap& struct_map, bool& reported_flag);

  std::shared_ptr<FilterConfig> config_;
  bool request_processing_finished_{false};
};

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
