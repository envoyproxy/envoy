#pragma once

#include "common/common/utility.h"

#include "extensions/filters/network/kafka/broker/filter.h"
#include "extensions/filters/network/kafka/external/request_metrics.h"
#include "extensions/filters/network/kafka/external/response_metrics.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

/**
 * Request callback responsible for updating state of related response decoder.
 * When a request gets successfully parsed, the response decoder registers a new incoming request.
 */
class Forwarder : public RequestCallback {
public:
  Forwarder(ResponseDecoder& response_decoder) : response_decoder_{response_decoder} {};

  /**
   * When request is successfully parsed, register expected response in request decoder.
   */
  void onMessage(AbstractRequestSharedPtr request) override;

  /**
   * Do nothing if request has parse failures.
   */
  void onFailedParse(RequestParseFailureSharedPtr) override;

private:
  ResponseDecoder& response_decoder_;
};

/**
 * Message callback that is used to update metrics.
 * Keeps requests' arrival timestamps (by correlation id) and uses them calculate response
 * processing time.
 */
class MetricTrackingCallback : public KafkaCallback {
public:
  MetricTrackingCallback(Stats::Scope& scope, TimeSource& time_source,
                         const std::string& stat_prefix);

  MetricTrackingCallback(TimeSource& time_source, RichRequestMetricsSharedPtr request_metrics,
                         RichResponseMetricsSharedPtr response_metrics);

  /**
   * When request is successfully parsed, update the metrics and persist the arrival timestamp.
   */
  void onMessage(AbstractRequestSharedPtr request) override;

  /**
   * When response is successfully parsed, compute the duration between request and response arrival
   * and update the metrics.
   */
  void onMessage(AbstractResponseSharedPtr response) override;

  /**
   * When request has parse failures, increase failure count.
   */
  void onFailedParse(RequestParseFailureSharedPtr) override;

  /**
   * When response has parse failures, increase failure count.
   */
  void onFailedParse(ResponseMetadataSharedPtr) override;

  std::map<int32_t, MonotonicTime>& getRequestArrivalsForTest();

private:
  TimeSource& time_source_;
  std::map<int32_t, MonotonicTime> request_arrivals_;
  RichRequestMetricsSharedPtr request_metrics_;
  RichResponseMetricsSharedPtr response_metrics_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
