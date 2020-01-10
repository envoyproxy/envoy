#include "extensions/filters/network/kafka/broker/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

void Forwarder::onMessage(AbstractRequestSharedPtr request) {
  const RequestHeader& header = request->request_header_;
  response_decoder_.expectResponse(header.correlation_id_, header.api_key_, header.api_version_);
}

void Forwarder::onFailedParse(RequestParseFailureSharedPtr parse_failure) {
  const RequestHeader& header = parse_failure->request_header_;
  response_decoder_.expectResponse(header.correlation_id_, header.api_key_, header.api_version_);
}

// Nothing fancy here, proper metrics registration is left to Rich...MetricsImpl constructors.
KafkaMetricsFacadeImpl::KafkaMetricsFacadeImpl(Stats::Scope& scope, TimeSource& time_source,
                                               const std::string& stat_prefix)
    : KafkaMetricsFacadeImpl{time_source,
                             std::make_shared<RichRequestMetricsImpl>(scope, stat_prefix),
                             std::make_shared<RichResponseMetricsImpl>(scope, stat_prefix)} {};

KafkaMetricsFacadeImpl::KafkaMetricsFacadeImpl(TimeSource& time_source,
                                               RichRequestMetricsSharedPtr request_metrics,
                                               RichResponseMetricsSharedPtr response_metrics)
    : time_source_{time_source}, request_metrics_{request_metrics}, response_metrics_{
                                                                        response_metrics} {};

// When request is successfully parsed, increase type count and store its arrival timestamp.
void KafkaMetricsFacadeImpl::onMessage(AbstractRequestSharedPtr request) {
  const RequestHeader& header = request->request_header_;
  request_metrics_->onRequest(header.api_key_);

  const MonotonicTime request_arrival_ts = time_source_.monotonicTime();
  request_arrivals_[header.correlation_id_] = request_arrival_ts;
}

void KafkaMetricsFacadeImpl::onFailedParse(RequestParseFailureSharedPtr) {
  request_metrics_->onUnknownRequest();
}

void KafkaMetricsFacadeImpl::onRequestException() { request_metrics_->onBrokenRequest(); }

// When response is successfully parsed, compute processing time using its correlation id and
// stored request arrival timestamp, then update metrics with the result.
void KafkaMetricsFacadeImpl::onMessage(AbstractResponseSharedPtr response) {
  const ResponseMetadata& metadata = response->metadata_;

  const MonotonicTime response_arrival_ts = time_source_.monotonicTime();
  const MonotonicTime request_arrival_ts = request_arrivals_[metadata.correlation_id_];
  request_arrivals_.erase(metadata.correlation_id_);

  const MonotonicTime::duration time_in_broker = response_arrival_ts - request_arrival_ts;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_in_broker);

  response_metrics_->onResponse(metadata.api_key_, ms.count());
}

void KafkaMetricsFacadeImpl::onFailedParse(ResponseMetadataSharedPtr) {
  response_metrics_->onUnknownResponse();
}

void KafkaMetricsFacadeImpl::onResponseException() { response_metrics_->onBrokenResponse(); }

absl::flat_hash_map<int32_t, MonotonicTime>& KafkaMetricsFacadeImpl::getRequestArrivalsForTest() {
  return request_arrivals_;
}

KafkaBrokerFilter::KafkaBrokerFilter(Stats::Scope& scope, TimeSource& time_source,
                                     const std::string& stat_prefix)
    : KafkaBrokerFilter{
          std::make_shared<KafkaMetricsFacadeImpl>(scope, time_source, stat_prefix)} {};

KafkaBrokerFilter::KafkaBrokerFilter(const KafkaMetricsFacadeSharedPtr& metrics)
    : metrics_{metrics}, response_decoder_{new ResponseDecoder({metrics})},
      request_decoder_{
          new RequestDecoder({std::make_shared<Forwarder>(*response_decoder_), metrics})} {};

KafkaBrokerFilter::KafkaBrokerFilter(KafkaMetricsFacadeSharedPtr metrics,
                                     ResponseDecoderSharedPtr response_decoder,
                                     RequestDecoderSharedPtr request_decoder)
    : metrics_{metrics}, response_decoder_{response_decoder}, request_decoder_{request_decoder} {};

Network::FilterStatus KafkaBrokerFilter::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void KafkaBrokerFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) {}

Network::FilterStatus KafkaBrokerFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "data from Kafka client [{} request bytes]", data.length());
  try {
    request_decoder_->onData(data);
    return Network::FilterStatus::Continue;
  } catch (const EnvoyException& e) {
    ENVOY_LOG(debug, "could not process data from Kafka client: {}", e.what());
    metrics_->onRequestException();
    request_decoder_->reset();
    return Network::FilterStatus::StopIteration;
  }
}

Network::FilterStatus KafkaBrokerFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "data from Kafka broker [{} response bytes]", data.length());
  try {
    response_decoder_->onData(data);
    return Network::FilterStatus::Continue;
  } catch (const EnvoyException& e) {
    ENVOY_LOG(debug, "could not process data from Kafka broker: {}", e.what());
    metrics_->onResponseException();
    response_decoder_->reset();
    return Network::FilterStatus::StopIteration;
  }
}

RequestDecoderSharedPtr KafkaBrokerFilter::getRequestDecoderForTest() { return request_decoder_; }

ResponseDecoderSharedPtr KafkaBrokerFilter::getResponseDecoderForTest() {
  return response_decoder_;
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
