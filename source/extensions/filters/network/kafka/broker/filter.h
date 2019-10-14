#pragma once

#include "envoy/common/time.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/external/request_metrics.h"
#include "extensions/filters/network/kafka/external/response_metrics.h"
#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/request_codec.h"
#include "extensions/filters/network/kafka/response_codec.h"

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
 */
class Forwarder : public RequestCallback {
public:
  /**
   * Binds forwarder to given response decoder.
   */
  Forwarder(ResponseDecoder& response_decoder) : response_decoder_{response_decoder} {};

  /**
   * When request is successfully parsed, register expected response in request decoder.
   */
  void onMessage(AbstractRequestSharedPtr request) override;

  /**
   * When request could not be recognized, we can extract the header, so we can register the
   * expected response.
   * Decoder will not be able to capable of decoding the response, but at least we will not break
   * the communication between client and broker.
   */
  void onFailedParse(RequestParseFailureSharedPtr) override;

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

  /**
   * When request is successfully parsed, increase type count and store its arrival timestamp.
   */
  void onMessage(AbstractRequestSharedPtr request) override;

  /**
   * When response is successfully parsed, compute processing time using its correlation id and
   * stored request arrival timestamp, then update metrics with the result.
   */
  void onMessage(AbstractResponseSharedPtr response) override;

  /**
   * If request could not be recognized, increase unknown request count.
   */
  void onFailedParse(RequestParseFailureSharedPtr) override;

  /**
   * If response could not be recognized, increase unknown response count.
   */
  void onFailedParse(ResponseMetadataSharedPtr) override;

  /**
   * If exceptions occurred while deserializing request, increase request failure count.
   */
  void onRequestException() override;

  /**
   * If exceptions occurred while deserializing response, increase response failure count.
   */
  void onResponseException() override;

  std::map<int32_t, MonotonicTime>& getRequestArrivalsForTest();

private:
  TimeSource& time_source_;
  std::map<int32_t, MonotonicTime> request_arrivals_;
  RichRequestMetricsSharedPtr request_metrics_;
  RichResponseMetricsSharedPtr response_metrics_;
};

/**
 * Implementation of Kafka broker-level filter.
 * Uses two decoders - request and response ones, that are connected using Forwarder instance.
 * There's also a KafkaMetricsFacade, that is listening on codec events.
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
 * +----------+------+       +----+----+        +---------+--------+
 *            |                   |                       ^
 *            |                   |                       |
 *            |                   v                       |
 *            |           +-------+-------+               |
 *            +---------->+ResponseDecoder+---------------+
 *                        +---------------+
 */
class KafkaBrokerFilter : public Network::Filter, private Logger::Loggable<Logger::Id::kafka> {
public:
  /**
   * Main constructor.
   * Creates decoders that eventually update prefixed metrics stored in scope, using time source for
   * duration calculation.
   */
  KafkaBrokerFilter(Stats::Scope& scope, TimeSource& time_source, const std::string& stat_prefix);

  /**
   * Visible for testing.
   */
  KafkaBrokerFilter(KafkaMetricsFacadeSharedPtr metrics, ResponseDecoderSharedPtr response_decoder,
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
  KafkaBrokerFilter(const KafkaMetricsFacadeSharedPtr& metrics);

  const KafkaMetricsFacadeSharedPtr metrics_;
  const ResponseDecoderSharedPtr response_decoder_;
  const RequestDecoderSharedPtr request_decoder_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
