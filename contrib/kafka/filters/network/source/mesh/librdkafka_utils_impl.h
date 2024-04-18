#pragma once

#include <vector>

#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

using RdKafkaPartitionPtr = std::unique_ptr<RdKafka::TopicPartition>;
using RdKafkaPartitionVector = std::vector<RdKafka::TopicPartition*>;

/**
 * Real implementation that just performs librdkafka operations.
 */
class LibRdKafkaUtilsImpl : public LibRdKafkaUtils {
public:
  // LibRdKafkaUtils
  RdKafka::Conf::ConfResult setConfProperty(RdKafka::Conf& conf, const std::string& name,
                                            const std::string& value,
                                            std::string& errstr) const override;

  // LibRdKafkaUtils
  RdKafka::Conf::ConfResult setConfDeliveryCallback(RdKafka::Conf& conf,
                                                    RdKafka::DeliveryReportCb* dr_cb,
                                                    std::string& errstr) const override;

  // LibRdKafkaUtils
  std::unique_ptr<RdKafka::Producer> createProducer(RdKafka::Conf* conf,
                                                    std::string& errstr) const override;

  // LibRdKafkaUtils
  std::unique_ptr<RdKafka::KafkaConsumer> createConsumer(RdKafka::Conf* conf,
                                                         std::string& errstr) const override;

  // LibRdKafkaUtils
  RdKafka::Headers* convertHeaders(
      const std::vector<std::pair<absl::string_view, absl::string_view>>& headers) const override;

  // LibRdKafkaUtils
  void deleteHeaders(RdKafka::Headers* librdkafka_headers) const override;

  // LibRdKafkaUtils
  ConsumerAssignmentConstPtr assignConsumerPartitions(RdKafka::KafkaConsumer& consumer,
                                                      const std::string& topic,
                                                      const int32_t partitions) const override;

  // Default singleton accessor.
  static const LibRdKafkaUtils& getDefaultInstance();
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
