#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils_impl.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// ConsumerAssignmentImpl

class ConsumerAssignmentImpl : public ConsumerAssignment {
public:
  ConsumerAssignmentImpl(std::vector<RdKafkaPartitionPtr>&& assignment)
      : assignment_{std::move(assignment)} {};

  // The assignment in a form that librdkafka likes.
  RdKafkaPartitionVector raw() const;

private:
  const std::vector<RdKafkaPartitionPtr> assignment_;
};

RdKafkaPartitionVector ConsumerAssignmentImpl::raw() const {
  RdKafkaPartitionVector result;
  for (const auto& tp : assignment_) {
    result.push_back(tp.get()); // Raw pointer.
  }
  return result;
}

// LibRdKafkaUtils

RdKafka::Conf::ConfResult LibRdKafkaUtilsImpl::setConfProperty(RdKafka::Conf& conf,
                                                               const std::string& name,
                                                               const std::string& value,
                                                               std::string& errstr) const {
  return conf.set(name, value, errstr);
}

RdKafka::Conf::ConfResult
LibRdKafkaUtilsImpl::setConfDeliveryCallback(RdKafka::Conf& conf, RdKafka::DeliveryReportCb* dr_cb,
                                             std::string& errstr) const {
  return conf.set("dr_cb", dr_cb, errstr);
}

std::unique_ptr<RdKafka::Producer> LibRdKafkaUtilsImpl::createProducer(RdKafka::Conf* conf,
                                                                       std::string& errstr) const {
  return std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(conf, errstr));
}

std::unique_ptr<RdKafka::KafkaConsumer>
LibRdKafkaUtilsImpl::createConsumer(RdKafka::Conf* conf, std::string& errstr) const {
  return std::unique_ptr<RdKafka::KafkaConsumer>(RdKafka::KafkaConsumer::create(conf, errstr));
}

RdKafka::Headers* LibRdKafkaUtilsImpl::convertHeaders(
    const std::vector<std::pair<absl::string_view, absl::string_view>>& headers) const {
  RdKafka::Headers* result = RdKafka::Headers::create();
  for (const auto& header : headers) {
    const RdKafka::Headers::Header librdkafka_header = {
        std::string(header.first), header.second.data(), header.second.length()};
    const auto ec = result->add(librdkafka_header);
    // This should never happen ('add' in 1.7.0 does not return any other error codes).
    if (RdKafka::ERR_NO_ERROR != ec) {
      delete result;
      return nullptr;
    }
  }
  return result;
}

void LibRdKafkaUtilsImpl::deleteHeaders(RdKafka::Headers* librdkafka_headers) const {
  delete librdkafka_headers;
}

ConsumerAssignmentConstPtr LibRdKafkaUtilsImpl::assignConsumerPartitions(
    RdKafka::KafkaConsumer& consumer, const std::string& topic, const int32_t partitions) const {

  // Construct the topic-partition vector that we are going to store.
  std::vector<RdKafkaPartitionPtr> assignment;
  for (int partition = 0; partition < partitions; ++partition) {

    // We consume records from the beginning of each partition.
    const int64_t initial_offset = 0;
    assignment.push_back(std::unique_ptr<RdKafka::TopicPartition>(
        RdKafka::TopicPartition::create(topic, partition, initial_offset)));
  }
  auto result = std::make_unique<ConsumerAssignmentImpl>(std::move(assignment));

  // Do the assignment.
  consumer.assign(result->raw());
  return result;
}

const LibRdKafkaUtils& LibRdKafkaUtilsImpl::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(LibRdKafkaUtilsImpl);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
