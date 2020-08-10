#pragma once

#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Kafka 'Produce' request, that is aimed at particular cluster.
 * A single Produce request coming from downstream can map into multiple entries,
 * as the topics can be hosted on different clusters.
 */
class ProduceRequestHolder : public AbstractInFlightRequest,
                             public ProduceFinishCb,
                             public std::enable_shared_from_this<ProduceRequestHolder> {
public:
  ProduceRequestHolder(AbstractRequestListener& filter,
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
  struct RecordFootmark {

    std::string topic_;
    int32_t partition_;
    absl::string_view key_;
    absl::string_view value_;

    int16_t error_code_;
    uint32_t saved_offset_;

    RecordFootmark(const std::string& topic, const int32_t partition, const absl::string_view key,
                   const absl::string_view value)
        : topic_{topic}, partition_{partition}, key_{key}, value_{value}, error_code_{0},
          saved_offset_{0} {};
  };

  static std::vector<RecordFootmark>
  computeFootmarks(const std::string& topic, const int32_t partition, const Bytes& records);

  static std::vector<RecordFootmark>
  processMagic2(const std::string& topic, const int32_t partition, absl::string_view sv);

  // Original request.
  const std::shared_ptr<Request<ProduceRequest>> request_;

  // How many responses from Kafka Producer handling our request do we expect.
  int expected_responses_;

  std::vector<RecordFootmark> footmarks_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
