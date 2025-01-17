#pragma once

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce_record_extractor.h"
#include "contrib/kafka/filters/network/source/mesh/outbound_record.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_facade.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Kafka 'Produce' request, that is aimed at particular cluster.
 * A single Produce request coming from downstream can map into multiple entries,
 * as the topics can be hosted on different clusters.
 *
 * These requests stored in 2 places: this filter (request's origin) and in RichKafkaProducer
 * instances (to match pure-Kafka confirmations to the requests).
 *
 *                               +--------------+
 *                               |<<librdkafka  |
 *                               |notification>>+--------+
 *                               +-+------------+        |
 *                                 |                     |
 *                                 |<notifies>           |
 *                                 |                     |
 *       +---------------+       +-v---------------+     |
 *       |KafkaMeshFilter+--+ +--+RichKafkaProducer|     |
 *       +-^-------------+  | |  +-----------------+     |
 *         |                | |                          |
 *         |     <in-flight>| |<requests-waiting         |
 *         |                | |for-delivery>             |
 *         |                | |                 <matches>|
 *         |       +--------v-v---------+                |
 *         +-------+ProduceRequestHolder|----------+     |
 * <notifies-      +---------+----------+<contains>|     |
 * when-finished>            |                     |     |
 *                 +---------v----------+          |     |
 *                 |PartitionProduceData|          |     |
 *                 +---------^----------+          |     |
 *                           |<absl::string_view>  |     |
 *         +-----------------+----------------+    |     |
 *         |                 |                |    |     |
 *   +-----+--------+ +------+-------+ +------+----v--+  |
 *   |OutboundRecord| |OutboundRecord| |OutboundRecord<--+
 *   +--------------+ +--------------+ +--------------+
 */
class ProduceRequestHolder : public BaseInFlightRequest,
                             public ProduceFinishCb,
                             public std::enable_shared_from_this<ProduceRequestHolder> {
public:
  ProduceRequestHolder(AbstractRequestListener& filter, UpstreamKafkaFacade& kafka_facade,
                       const std::shared_ptr<Request<ProduceRequest>> request);

  // Visible for testing.
  ProduceRequestHolder(AbstractRequestListener& filter, UpstreamKafkaFacade& kafka_facade,
                       const RecordExtractor& record_extractor,
                       const std::shared_ptr<Request<ProduceRequest>> request);

  // AbstractInFlightRequest
  void startProcessing() override;

  // AbstractInFlightRequest
  bool finished() const override;

  // AbstractInFlightRequest
  AbstractResponseSharedPtr computeAnswer() const override;

  // ProduceFinishCb
  bool accept(const DeliveryMemento& memento) override;

private:
  // Access to Kafka producers pointing to upstream Kafka clusters.
  UpstreamKafkaFacade& kafka_facade_;

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
