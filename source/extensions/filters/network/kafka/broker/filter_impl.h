#include <sstream>

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "extensions/filters/network/kafka/external/request_metrics.h"
#include "extensions/filters/network/kafka/external/response_metrics.h"
#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/kafka_response.h"

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

  void onMessage(AbstractRequestSharedPtr request) override {
    const RequestHeader& header = request->request_header_;
    response_decoder_.expectResponse(header.api_key_, header.api_version_);
  }

  void onFailedParse(RequestParseFailureSharedPtr) override {}

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
                         const std::string& stat_prefix)
      : MetricTrackingCallback{time_source,
                               std::make_shared<RichRequestMetricsImpl>(scope, stat_prefix),
                               std::make_shared<RichResponseMetricsImpl>(scope, stat_prefix)} {};

  MetricTrackingCallback(TimeSource& time_source, RichRequestMetricsSharedPtr request_metrics,
                         RichResponseMetricsSharedPtr response_metrics)
      : time_source_{time_source}, request_metrics_{request_metrics}, response_metrics_{
                                                                          response_metrics} {};

  /**
   * When request is successfully parsed, update the metrics and persist the arrival timestamp.
   */
  void onMessage(AbstractRequestSharedPtr request) override {
    const RequestHeader& header = request->request_header_;
    request_metrics_->onMessage(header.api_key_);

    const MonotonicTime request_arrival_ts = time_source_.monotonicTime();
    request_arrivals_[header.correlation_id_] = request_arrival_ts;
  }

  /**
   * When response is successfully parsed, update the metrics and
   */
  void onMessage(AbstractResponseSharedPtr response) override {
    const ResponseMetadata& metadata = response->metadata_;

    const MonotonicTime response_arrival_ts = time_source_.monotonicTime();
    const MonotonicTime request_arrival_ts = request_arrivals_[metadata.correlation_id_];
    request_arrivals_.erase(metadata.correlation_id_);

    const MonotonicTime::duration time_in_broker = response_arrival_ts - request_arrival_ts;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_in_broker);

    response_metrics_->onMessage(metadata.api_key_, ms.count());
  }

  void onFailedParse(RequestParseFailureSharedPtr) override { request_metrics_->onFailure(); }

  void onFailedParse(ResponseMetadataSharedPtr) override { response_metrics_->onFailure(); }

  std::map<int32_t, MonotonicTime>& getRequestArrivalsForTest() { return request_arrivals_; }

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
