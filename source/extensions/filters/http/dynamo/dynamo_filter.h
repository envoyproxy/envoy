#pragma once

#include <cstdint>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats.h"

#include "common/json/json_loader.h"

#include "extensions/filters/http/dynamo/dynamo_request_parser.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

/**
 * DynamoDb filter to process egress request to dynamo and capture comprehensive stats
 * It captures RPS/latencies:
 *  1) Per table per response code (and group of response codes, e.g., 2xx/3xx/etc)
 *  2) Per operation per response code (and group of response codes, e.g., 2xx/3xx/etc)
 */
class DynamoFilter : public Http::StreamFilter {
public:
  DynamoFilter(Runtime::Loader& runtime, const std::string& stat_prefix, Stats::Scope& scope)
      : runtime_(runtime), stat_prefix_(stat_prefix + "dynamodb."), scope_(scope) {
    enabled_ = runtime_.snapshot().featureEnabled("dynamodb.filter_enabled", 100);
  }

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  void onDecodeComplete(const Buffer::Instance& data);
  void onEncodeComplete(const Buffer::Instance& data);
  std::string buildBody(const Buffer::Instance* buffered, const Buffer::Instance& last);
  void chargeBasicStats(uint64_t status);
  void chargeStatsPerEntity(const std::string& entity, const std::string& entity_type,
                            uint64_t status);
  void chargeFailureSpecificStats(const Json::Object& json_body);
  void chargeUnProcessedKeysStats(const Json::Object& json_body);
  void chargeTablePartitionIdStats(const Json::Object& json_body);

  Runtime::Loader& runtime_;
  std::string stat_prefix_;
  Stats::Scope& scope_;

  bool enabled_{};
  std::string operation_{};
  RequestParser::TableDescriptor table_descriptor_{"", true};
  std::string error_type_{};
  MonotonicTime start_decode_;
  Http::HeaderMap* response_headers_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
