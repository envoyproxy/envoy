#include "extensions/filters/http/dynamo/dynamo_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/http/codes.h"
#include "common/http/exception.h"
#include "common/http/utility.h"
#include "common/json/json_loader.h"

#include "extensions/filters/http/dynamo/dynamo_request_parser.h"
#include "extensions/filters/http/dynamo/dynamo_stats.h"

#include "absl/container/fixed_array.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

Http::FilterHeadersStatus DynamoFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (enabled_) {
    start_decode_ = time_source_.monotonicTime();
    operation_ = RequestParser::parseOperation(headers);
    return Http::FilterHeadersStatus::StopIteration;
  } else {
    return Http::FilterHeadersStatus::Continue;
  }
}

Http::FilterDataStatus DynamoFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (enabled_ && end_stream) {
    onDecodeComplete(data);
  }

  if (!enabled_ || end_stream) {
    return Http::FilterDataStatus::Continue;
  } else {
    // Buffer until the complete request has been processed.
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Http::FilterTrailersStatus DynamoFilter::decodeTrailers(Http::RequestTrailerMap&) {
  if (enabled_) {
    Buffer::OwnedImpl empty;
    onDecodeComplete(empty);
  }

  return Http::FilterTrailersStatus::Continue;
}

void DynamoFilter::onDecodeComplete(const Buffer::Instance& data) {
  std::string body = buildBody(decoder_callbacks_->decodingBuffer(), data);
  if (!body.empty()) {
    try {
      Json::ObjectSharedPtr json_body = Json::Factory::loadFromString(body);
      table_descriptor_ = RequestParser::parseTable(operation_, *json_body);
    } catch (const Json::Exception& jsonEx) {
      // Body parsing failed. This should not happen, just put a stat for that.
      stats_->incCounter({stats_->invalid_req_body_});
    }
  }
}

void DynamoFilter::onEncodeComplete(const Buffer::Instance& data) {
  ASSERT(enabled_);
  uint64_t status = Http::Utility::getResponseStatus(*response_headers_);
  chargeBasicStats(status);

  std::string body = buildBody(encoder_callbacks_->encodingBuffer(), data);
  if (!body.empty()) {
    try {
      Json::ObjectSharedPtr json_body = Json::Factory::loadFromString(body);
      chargeTablePartitionIdStats(*json_body);

      if (Http::CodeUtility::is4xx(status)) {
        chargeFailureSpecificStats(*json_body);
      }
      // Batch Operations will always return status 200 for a partial or full success. Check
      // unprocessed keys to determine partial success.
      // http://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Programming.Errors.html#Programming.Errors.BatchOperations
      if (RequestParser::isBatchOperation(operation_)) {
        chargeUnProcessedKeysStats(*json_body);
      }
    } catch (const Json::Exception&) {
      // Body parsing failed. This should not happen, just put a stat for that.
      stats_->incCounter({stats_->invalid_resp_body_});
    }
  }
}

Http::FilterHeadersStatus DynamoFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                      bool end_stream) {
  Http::FilterHeadersStatus status = Http::FilterHeadersStatus::Continue;
  if (enabled_) {
    response_headers_ = &headers;

    if (end_stream) {
      Buffer::OwnedImpl empty;
      onEncodeComplete(empty);
    } else {
      status = Http::FilterHeadersStatus::StopIteration;
    }
  }

  return status;
}

Http::FilterDataStatus DynamoFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (enabled_ && end_stream) {
    onEncodeComplete(data);
  }

  if (!enabled_ || end_stream) {
    return Http::FilterDataStatus::Continue;
  } else {
    // Buffer until the complete response has been processed.
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

Http::FilterTrailersStatus DynamoFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (enabled_) {
    Buffer::OwnedImpl empty;
    onEncodeComplete(empty);
  }

  return Http::FilterTrailersStatus::Continue;
}

std::string DynamoFilter::buildBody(const Buffer::Instance* buffered,
                                    const Buffer::Instance& last) {
  std::string body;
  if (buffered) {
    uint64_t num_slices = buffered->getRawSlices(nullptr, 0);
    absl::FixedArray<Buffer::RawSlice> slices(num_slices);
    buffered->getRawSlices(slices.begin(), num_slices);
    for (const Buffer::RawSlice& slice : slices) {
      body.append(static_cast<const char*>(slice.mem_), slice.len_);
    }
  }

  uint64_t num_slices = last.getRawSlices(nullptr, 0);
  absl::FixedArray<Buffer::RawSlice> slices(num_slices);
  last.getRawSlices(slices.begin(), num_slices);
  for (const Buffer::RawSlice& slice : slices) {
    body.append(static_cast<const char*>(slice.mem_), slice.len_);
  }

  return body;
}

void DynamoFilter::chargeBasicStats(uint64_t status) {
  if (!operation_.empty()) {
    chargeStatsPerEntity(operation_, "operation", status);
  } else {
    stats_->incCounter({stats_->operation_missing_});
  }

  if (!table_descriptor_.table_name.empty()) {
    chargeStatsPerEntity(table_descriptor_.table_name, "table", status);
  } else if (table_descriptor_.is_single_table) {
    stats_->incCounter({stats_->table_missing_});
  } else {
    stats_->incCounter({stats_->multiple_tables_});
  }
}

void DynamoFilter::chargeStatsPerEntity(const std::string& entity, const std::string& entity_type,
                                        uint64_t status) {
  std::chrono::milliseconds latency = std::chrono::duration_cast<std::chrono::milliseconds>(
      time_source_.monotonicTime() - start_decode_);

  size_t group_index = DynamoStats::groupIndex(status);
  Stats::StatNameDynamicPool dynamic(stats_->symbolTable());

  const Stats::StatName entity_type_name =
      stats_->getBuiltin(entity_type, stats_->unknown_entity_type_);
  const Stats::StatName entity_name = dynamic.add(entity);

  // TODO(jmarantz): Consider using a similar mechanism to common/http/codes.cc
  // to avoid creating dynamic stat-names for common statuses.
  const Stats::StatName total_name = dynamic.add(absl::StrCat("upstream_rq_total_", status));
  const Stats::StatName time_name = dynamic.add(absl::StrCat("upstream_rq_time_", status));

  stats_->incCounter({entity_type_name, entity_name, stats_->upstream_rq_total_});
  const Stats::StatName total_group = stats_->upstream_rq_total_groups_[group_index];
  stats_->incCounter({entity_type_name, entity_name, total_group});
  stats_->incCounter({entity_type_name, entity_name, total_name});

  stats_->recordHistogram({entity_type_name, entity_name, stats_->upstream_rq_time_},
                          Stats::Histogram::Unit::Milliseconds, latency.count());
  const Stats::StatName time_group = stats_->upstream_rq_time_groups_[group_index];
  stats_->recordHistogram({entity_type_name, entity_name, time_group},
                          Stats::Histogram::Unit::Milliseconds, latency.count());
  stats_->recordHistogram({entity_type_name, entity_name, time_name},
                          Stats::Histogram::Unit::Milliseconds, latency.count());
}

void DynamoFilter::chargeUnProcessedKeysStats(const Json::Object& json_body) {
  // The unprocessed keys block contains a list of tables and keys for that table that did not
  // complete apart of the batch operation. Only the table names will be logged for errors.
  std::vector<std::string> unprocessed_tables = RequestParser::parseBatchUnProcessedKeys(json_body);
  for (const std::string& unprocessed_table : unprocessed_tables) {
    Stats::StatNameDynamicStorage storage(unprocessed_table, stats_->symbolTable());
    stats_->incCounter(
        {stats_->error_, storage.statName(), stats_->batch_failure_unprocessed_keys_});
  }
}

void DynamoFilter::chargeFailureSpecificStats(const Json::Object& json_body) {
  std::string error_type = RequestParser::parseErrorType(json_body);

  if (!error_type.empty()) {
    Stats::StatNameDynamicPool dynamic(stats_->symbolTable());
    if (table_descriptor_.table_name.empty()) {
      stats_->incCounter({stats_->error_, stats_->no_table_, dynamic.add(error_type)});
    } else {
      stats_->incCounter(
          {stats_->error_, dynamic.add(table_descriptor_.table_name), dynamic.add(error_type)});
    }
  } else {
    stats_->incCounter({stats_->empty_response_body_});
  }
}

void DynamoFilter::chargeTablePartitionIdStats(const Json::Object& json_body) {
  if (table_descriptor_.table_name.empty() || operation_.empty()) {
    return;
  }

  std::vector<RequestParser::PartitionDescriptor> partitions =
      RequestParser::parsePartitions(json_body);
  for (const RequestParser::PartitionDescriptor& partition : partitions) {
    stats_
        ->buildPartitionStatCounter(table_descriptor_.table_name, operation_,
                                    partition.partition_id_)
        .add(partition.capacity_);
  }
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
