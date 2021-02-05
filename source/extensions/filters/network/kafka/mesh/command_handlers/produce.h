#pragma once

#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Binds a single inbound record from Kafka client with its delivery information.
struct OutboundRecord {

  // These fields were received from downstream.
  std::string topic_;
  int32_t partition_;
  absl::string_view key_;
  absl::string_view value_;

  // These fields get updated when delivery to upstream Kafka cluster finishes.
  int16_t error_code_;
  uint32_t saved_offset_;

  OutboundRecord(const std::string& topic, const int32_t partition, const absl::string_view key,
                 const absl::string_view value)
      : topic_{topic}, partition_{partition}, key_{key}, value_{value}, error_code_{0},
        saved_offset_{0} {};
};

// Dependency injection class responsible for extracting records out of produce request's contents.
class RecordExtractor {
public:
  virtual ~RecordExtractor() = default;

  virtual std::vector<OutboundRecord>
  extractRecords(const std::vector<TopicProduceData>& data) const PURE;
};

class RecordExtractorImpl : public RecordExtractor {
public:
  std::vector<OutboundRecord>
  extractRecords(const std::vector<TopicProduceData>& data) const override;

  static absl::string_view extractElement(absl::string_view& input);

private:
  std::vector<OutboundRecord> extractRecordsForTopic(const std::string& topic,
                                                     const int32_t partition,
                                                     const Bytes& records) const;

  // Impl note: I'm sorry for the long name.
  std::vector<OutboundRecord> extractRecordsOutOfBatchWithMagicEqualTo2(const std::string& topic,
                                                                        const int32_t partition,
                                                                        absl::string_view sv) const;

  OutboundRecord extractRecord(const std::string& topic, const int32_t partition,
                               absl::string_view& data) const;
};

/**
 * Kafka 'Produce' request, that is aimed at particular cluster.
 * A single Produce request coming from downstream can map into multiple entries,
 * as the topics can be hosted on different clusters.
 */
class ProduceRequestHolder : public BaseInFlightRequest,
                             public ProduceFinishCb,
                             public std::enable_shared_from_this<ProduceRequestHolder> {
public:
  ProduceRequestHolder(AbstractRequestListener& filter,
                       const std::shared_ptr<Request<ProduceRequest>> request);

  // Visible for testing.
  ProduceRequestHolder(AbstractRequestListener& filter, const RecordExtractor& record_extractor,
                       const std::shared_ptr<Request<ProduceRequest>> request);

  // AbstractInFlightRequest
  void invoke(UpstreamKafkaFacade&) override;

  // AbstractInFlightRequest
  bool finished() const override;

  // AbstractInFlightRequest
  AbstractResponseSharedPtr computeAnswer() const override;

  // ProduceFinishCb
  bool accept(const DeliveryMemento& memento) override;

private:
  // Original request.
  const std::shared_ptr<Request<ProduceRequest>> request_;

  // How many responses from Kafka Producer handling our request we still expect.
  // This value decreases to 0 as we get confirmations from Kafka (successful or not).
  int expected_responses_;

  // Real records extracted out of request.
  std::vector<OutboundRecord> outbound_records_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
