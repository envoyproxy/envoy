#pragma once

#include <sstream>

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/codec.h"
#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/kafka_response.h"
#include "extensions/filters/network/kafka/metrics_holder.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

// === STATS ===================================================================

/**
 * All Kafka stats. @see stats_macros.h
 */
// clang-format off
#define KAFKA_STATS(COUNTER)                                                                \
  COUNTER(filters_created)                                                                  \
// clang-format on

/**
 * Struct definition for all Kafka stats. @see stats_macros.h
 */
struct InstanceStats {
  KAFKA_STATS(GENERATE_COUNTER_STRUCT)
};

// === FILTER ==================================================================

/**
 * Implementation of a basic kafka filter.
 */
class KafkaFilter : public Network::Filter {

public:
  KafkaFilter(Stats::Scope& scope, std::shared_ptr<MetricsHolder>);
  virtual ~KafkaFilter() {}

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

private:
  Network::ReadFilterCallbacks* read_callbacks_{};

  InstanceStats stats_;
  std::shared_ptr<MetricsHolder> metrics_holder_;

  // in future, we might have some shared state shared between both request & response decoder
  // for e.g. matching responses with requests (by correlation id)
  RequestDecoder request_decoder_;
  ResponseDecoder response_decoder_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
