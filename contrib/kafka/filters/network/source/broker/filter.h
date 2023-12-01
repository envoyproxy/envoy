#pragma once

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/broker/rewriter.h"
#include "contrib/kafka/filters/network/source/external/request_metrics.h"
#include "contrib/kafka/filters/network/source/external/response_metrics.h"
#include "contrib/kafka/filters/network/source/parser.h"
#include "contrib/kafka/filters/network/source/request_codec.h"
#include "contrib/kafka/filters/network/source/response_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

/**
 * Utility class that merges both request & response callbacks.
 */
class KafkaCallback : public RequestCallback, public ResponseCallback {};

using KafkaCallbackSharedPtr = std::shared_ptr<KafkaCallback>;

/**
 * Request callback responsible for updating state of related response decoder.
 * When a request gets successfully parsed, the response decoder registers a new incoming request.
 * When request could not be recognized, we can extract the header, so we can register the
 * expected response (decoder will not be able to capable of decoding the response, but at least we
 * will not break the communication between client and broker).
 */
class Forwarder : public RequestCallback {
public:
  /**
   * Binds forwarder to given response decoder.
   */
  Forwarder(ResponseDecoder& response_decoder) : response_decoder_{response_decoder} {};

  // RequestCallback
  void onMessage(AbstractRequestSharedPtr request) override;
  void onFailedParse(RequestParseFailureSharedPtr parse_failure) override;

private:
  ResponseDecoder& response_decoder_;
};

/**
 * Single access point for all Kafka-related metrics.
 * Implements Kafka message callback, so decoders can refer to it when parse results appear.
 * This interface was extracted to facilitate mock injection in unit tests.
 */
class KafkaMetricsFacade : public KafkaCallback {
public:
  /**
   * To be invoked when exceptions occur while processing a request.
   */
  virtual void onRequestException() PURE;

  /**
   * To be invoked when exceptions occur while processing a response.
   */
  virtual void onResponseException() PURE;
};

using KafkaMetricsFacadeSharedPtr = std::shared_ptr<KafkaMetricsFacade>;

/**
 * Metrics facade implementation that actually uses rich request/response metrics.
 * Keeps requests' arrival timestamps (by correlation id) and uses them calculate response
 * processing time.
 */
class KafkaMetricsFacadeImpl : public KafkaMetricsFacade {
public:
  /**
   * Creates facade that keeps prefixed metrics in given scope, and uses given time source to
   * compute processing durations.
   */
  KafkaMetricsFacadeImpl(Stats::Scope& scope, TimeSource& time_source,
                         const std::string& stat_prefix);

  /**
   * Visible for testing.
   */
  KafkaMetricsFacadeImpl(TimeSource& time_source, RichRequestMetricsSharedPtr request_metrics,
                         RichResponseMetricsSharedPtr response_metrics);

  // RequestCallback
  void onMessage(AbstractRequestSharedPtr request) override;
  void onFailedParse(RequestParseFailureSharedPtr parse_failure) override;

  // ResponseCallback
  void onMessage(AbstractResponseSharedPtr response) override;
  void onFailedParse(ResponseMetadataSharedPtr parse_failure) override;

  // KafkaMetricsFacade
  void onRequestException() override;
  void onResponseException() override;

  absl::flat_hash_map<int32_t, MonotonicTime>& getRequestArrivalsForTest();

private:
  TimeSource& time_source_;
  absl::flat_hash_map<int32_t, MonotonicTime> request_arrivals_;
  RichRequestMetricsSharedPtr request_metrics_;
  RichResponseMetricsSharedPtr response_metrics_;
};

/**
 * Implementation of Kafka broker-level filter.
 * Uses two decoders - request and response ones, that are connected using Forwarder instance.
 * KafkaMetricsFacade is listening for both request/response events to keep metrics.
 * ResponseRewriter is listening for response events to capture and rewrite them if needed.
 *
 *        +---------------------------------------------------+
 *        |                                                   |
 *        |               +--------------+                    |
 *        |   +---------->+RequestDecoder+----------------+   |
 *        |   |           +-------+------+                |   |
 *        |   |                   |                       |   |
 *        |   |                   |                       |   |
 *        |   |                   v                       v   v
 * +------+---+------+       +----+----+        +---------+---+----+
 * |KafkaBrokerFilter|       |Forwarder|        |KafkaMetricsFacade|
 * +------+---+------+       +----+----+        +---------+--------+
 *        |   |                   |                       ^
 *        |   |                   |                       |
 *        |   |                   v                       |
 *        |   |           +-------+-------+               |
 *        |   +---------->+ResponseDecoder+---------------+
 *        |               +-------+-------+
 *        |                       |
 *        |                       v
 *        |               +-------+--------+
 *        +-------------->+ResponseRewriter+
 *                        +----------------+
 */
class KafkaBrokerFilter : public Network::Filter, private Logger::Loggable<Logger::Id::kafka> {
public:
  /**
   * Main constructor.
   * Creates decoders that eventually update prefixed metrics stored in scope, using time source for
   * duration calculation.
   */
  KafkaBrokerFilter(Stats::Scope& scope, TimeSource& time_source,
                    const BrokerFilterConfig& filter_config);

  /**
   * Visible for testing.
   */
  KafkaBrokerFilter(KafkaMetricsFacadeSharedPtr metrics,
                    ResponseRewriterSharedPtr response_rewriter,
                    ResponseDecoderSharedPtr response_decoder,
                    RequestDecoderSharedPtr request_decoder);

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  RequestDecoderSharedPtr getRequestDecoderForTest();
  ResponseDecoderSharedPtr getResponseDecoderForTest();

private:
  /**
   * Helper delegate constructor.
   * Passes metrics facade as argument to decoders.
   */
  KafkaBrokerFilter(const BrokerFilterConfig& filter_config,
                    const KafkaMetricsFacadeSharedPtr& metrics);

  const KafkaMetricsFacadeSharedPtr metrics_;
  const ResponseRewriterSharedPtr response_rewriter_;
  const ResponseDecoderSharedPtr response_decoder_;
  const RequestDecoderSharedPtr request_decoder_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
