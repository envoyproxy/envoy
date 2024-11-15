#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer_impl.h"

#include "contrib/kafka/filters/network/source/kafka_types.h"
#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

RichKafkaConsumer::RichKafkaConsumer(InboundRecordProcessor& record_processor,
                                     Thread::ThreadFactory& thread_factory,
                                     const std::string& topic, const int32_t partition_count,
                                     const RawKafkaConfig& configuration)
    : RichKafkaConsumer(record_processor, thread_factory, topic, partition_count, configuration,
                        LibRdKafkaUtilsImpl::getDefaultInstance()){};

RichKafkaConsumer::RichKafkaConsumer(InboundRecordProcessor& record_processor,
                                     Thread::ThreadFactory& thread_factory,
                                     const std::string& topic, const int32_t partition_count,
                                     const RawKafkaConfig& configuration,
                                     const LibRdKafkaUtils& utils)
    : record_processor_{record_processor}, topic_{topic} {

  ENVOY_LOG(debug, "Creating consumer for topic [{}] with {} partitions", topic, partition_count);

  // Create consumer configuration object.
  std::unique_ptr<RdKafka::Conf> conf =
      std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

  std::string errstr;

  // Setup consumer custom properties.
  for (const auto& e : configuration) {
    ENVOY_LOG(debug, "Setting consumer property {}={}", e.first, e.second);
    if (utils.setConfProperty(*conf, e.first, e.second, errstr) != RdKafka::Conf::CONF_OK) {
      throw EnvoyException(absl::StrCat("Could not set consumer property [", e.first, "] to [",
                                        e.second, "]:", errstr));
    }
  }

  // We create the consumer.
  consumer_ = utils.createConsumer(conf.get(), errstr);
  if (!consumer_) {
    throw EnvoyException(absl::StrCat("Could not create consumer:", errstr));
  }

  // Consumer is going to read from all the topic partitions.
  assignment_ = utils.assignConsumerPartitions(*consumer_, topic, partition_count);

  // Start the worker thread.
  worker_thread_active_ = true;
  const std::function<void()> thread_routine = [this]() -> void { runWorkerLoop(); };
  worker_thread_ = thread_factory.createThread(thread_routine);
}

RichKafkaConsumer::~RichKafkaConsumer() {
  ENVOY_LOG(debug, "Closing Kafka consumer [{}]", topic_);

  worker_thread_active_ = false;
  // This should take at most INTEREST_TIMEOUT_MS + POLL_TIMEOUT_MS.
  worker_thread_->join();

  consumer_->unassign();
  consumer_->close();

  ENVOY_LOG(debug, "Kafka consumer [{}] closed succesfully", topic_);
}

// Read timeout constants.
// Large values are okay, but make the Envoy shutdown take longer
// (as there is no good way to interrupt a Kafka 'consume' call).
// XXX (adam.kotwasinski) This could be made configurable.

// How long a thread should wait for interest before checking if it's cancelled.
constexpr int32_t INTEREST_TIMEOUT_MS = 1000;

// How long a consumer should poll Kafka for messages.
constexpr int32_t POLL_TIMEOUT_MS = 1000;

void RichKafkaConsumer::runWorkerLoop() {
  while (worker_thread_active_) {

    // It makes no sense to poll and receive records if there is no interest right now,
    // so we can just block instead.
    bool can_poll = record_processor_.waitUntilInterest(topic_, INTEREST_TIMEOUT_MS);
    if (!can_poll) {
      // There is nothing to do, so we keep checking again.
      // Also we happen to check if we were closed - this makes Envoy shutdown bit faster.
      continue;
    }

    // There is interest in messages present in this topic, so we can start polling.
    std::vector<InboundRecordSharedPtr> records = receiveRecordBatch();
    for (auto& record : records) {
      record_processor_.receive(record);
    }
  }
  ENVOY_LOG(debug, "Worker thread for consumer [{}] finished", topic_);
}

// Helper method, converts byte array.
static NullableBytes toBytes(const void* data, const size_t size) {
  const unsigned char* as_char = static_cast<const unsigned char*>(data);
  if (data) {
    Bytes bytes(as_char, as_char + size);
    return {bytes};
  } else {
    return absl::nullopt;
  }
}

// Helper method, gets rid of librdkafka.
static InboundRecordSharedPtr transform(RdKafkaMessagePtr arg) {
  const auto topic = arg->topic_name();
  const auto partition = arg->partition();
  const auto offset = arg->offset();

  const NullableBytes key = toBytes(arg->key_pointer(), arg->key_len());
  const NullableBytes value = toBytes(arg->payload(), arg->len());

  return std::make_shared<InboundRecord>(topic, partition, offset, key, value);
}

std::vector<InboundRecordSharedPtr> RichKafkaConsumer::receiveRecordBatch() {
  // This message kicks off librdkafka consumer's Fetch requests and delivers a message.
  auto message = std::unique_ptr<RdKafka::Message>(consumer_->consume(POLL_TIMEOUT_MS));
  if (RdKafka::ERR_NO_ERROR == message->err()) {
    // We got a message.
    auto inbound_record = transform(std::move(message));
    ENVOY_LOG(trace, "Received Kafka message (first one): {}", inbound_record->toString());

    // XXX (adam.kotwasinski) There could be something more present in the consumer,
    // and we could drain it (at least a little) in the next commits.
    // See: https://github.com/confluentinc/librdkafka/discussions/3897
    return {inbound_record};
  } else {
    // Nothing extraordinary (timeout because there is nothing upstream),
    // or upstream connectivity failure.
    ENVOY_LOG(trace, "No message received in consumer [{}]: {}/{}", topic_,
              static_cast<int>(message->err()), RdKafka::err2str(message->err()));
    return {};
  }
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
