#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class LibRdKafkaUtilsImpl : public LibRdKafkaUtils {

  // LibRdKafkaUtils
  RdKafka::Conf::ConfResult setConfProperty(RdKafka::Conf& conf, const std::string& name,
                                            const std::string& value,
                                            std::string& errstr) const override {
    return conf.set(name, value, errstr);
  }

  // LibRdKafkaUtils
  RdKafka::Conf::ConfResult setConfDeliveryCallback(RdKafka::Conf& conf,
                                                    RdKafka::DeliveryReportCb* dr_cb,
                                                    std::string& errstr) const override {
    return conf.set("dr_cb", dr_cb, errstr);
  }

  // LibRdKafkaUtils
  std::unique_ptr<RdKafka::Producer> createProducer(RdKafka::Conf* conf,
                                                    std::string& errstr) const override {
    return std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(conf, errstr));
  }
};

RichKafkaProducer::RichKafkaProducer(Event::Dispatcher& dispatcher,
                                     Thread::ThreadFactory& thread_factory,
                                     const RawKafkaProducerConfig& configuration)
    : RichKafkaProducer(dispatcher, thread_factory, configuration, LibRdKafkaUtilsImpl{}){};

RichKafkaProducer::RichKafkaProducer(Event::Dispatcher& dispatcher,
                                     Thread::ThreadFactory& thread_factory,
                                     const RawKafkaProducerConfig& configuration,
                                     const LibRdKafkaUtils& utils)
    : dispatcher_{dispatcher} {

  // Create producer configuration object.
  std::unique_ptr<RdKafka::Conf> conf =
      std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
  std::string errstr;

  // Setup producer custom properties.
  for (const auto& e : configuration) {
    if (utils.setConfProperty(*conf, e.first, e.second, errstr) != RdKafka::Conf::CONF_OK) {
      throw EnvoyException(absl::StrCat("Could not set producer property [", e.first, "] to [",
                                        e.second, "]:", errstr));
    }
  }

  // Setup callback (this callback is going to be invoked in dedicated monitoring thread).
  if (utils.setConfDeliveryCallback(*conf, this, errstr) != RdKafka::Conf::CONF_OK) {
    throw EnvoyException(absl::StrCat("Could not set producer callback:", errstr));
  }

  // Finally, we create the producer.
  producer_ = utils.createProducer(conf.get(), errstr);
  if (!producer_) {
    throw EnvoyException(absl::StrCat("Could not create producer:", errstr));
  }

  // Start the monitoring thread.
  poller_thread_active_ = true;
  std::function<void()> thread_routine = [this]() -> void { checkDeliveryReports(); };
  poller_thread_ = thread_factory.createThread(thread_routine);
}

RichKafkaProducer::~RichKafkaProducer() {
  ENVOY_LOG(debug, "Shutting down worker thread");
  poller_thread_active_ = false; // This should never be needed, as we call 'markFinished' earlier.
  poller_thread_->join();
  ENVOY_LOG(debug, "Worker thread shut down successfully");
}

void RichKafkaProducer::markFinished() { poller_thread_active_ = false; }

void RichKafkaProducer::send(const ProduceFinishCbSharedPtr origin, const std::string& topic,
                             const int32_t partition, const absl::string_view key,
                             const absl::string_view value) {
  {
    void* value_data = const_cast<char*>(value.data()); // Needed for Kafka API.
    // Data is a pointer into request internals, and it is going to be managed by
    // ProduceRequestHolder lifecycle. So we are not going to use any of librdkafka's memory
    // management.
    const int flags = 0;
    const RdKafka::ErrorCode ec = producer_->produce(
        topic, partition, flags, value_data, value.size(), key.data(), key.size(), 0, nullptr);
    if (RdKafka::ERR_NO_ERROR == ec) {
      // We have succeeded with submitting data to producer, so we register a callback.
      unfinished_produce_requests_.push_back(origin);
    } else {
      // We could not submit data to producer.
      // Let's treat that as a normal failure (Envoy is a broker after all) and propagate
      // downstream.
      ENVOY_LOG(trace, "Produce failure: {}, while sending to [{}/{}]", ec, topic, partition);
      const DeliveryMemento memento = {value_data, ec, 0};
      origin->accept(memento);
    }
  }
}

void RichKafkaProducer::checkDeliveryReports() {
  while (poller_thread_active_) {
    // We are going to wait for 1000ms, returning when an event (message delivery) happens or
    // producer is closed. Unfortunately we do not have any ability to interrupt this call, so every
    // destructor is going to take up to this much time.
    producer_->poll(1000);
    // This invokes the callback below, if any delivery finished (successful or not).
  }
  ENVOY_LOG(debug, "Poller thread finished");
}

// Kafka callback that contains the delivery information.
void RichKafkaProducer::dr_cb(RdKafka::Message& message) {
  ENVOY_LOG(trace, "Delivery finished: {}, payload has been saved at offset {} in {}/{}",
            message.err(), message.topic_name(), message.partition(), message.offset());
  const DeliveryMemento memento = {message.payload(), message.err(), message.offset()};
  // Because this method gets executed in poller thread, we need to pass the data through
  // dispatcher.
  const Event::PostCb callback = [this, memento]() -> void { processDelivery(memento); };
  dispatcher_.post(callback);
}

// We got the delivery data.
// Now we just check all unfinished requests, find the one that originated this particular delivery,
// and notify it.
void RichKafkaProducer::processDelivery(const DeliveryMemento& memento) {
  for (auto it = unfinished_produce_requests_.begin(); it != unfinished_produce_requests_.end();) {
    bool accepted = (*it)->accept(memento);
    if (accepted) {
      unfinished_produce_requests_.erase(it);
      break; // This is important - a single request can be mapped into multiple callbacks here.
    } else {
      ++it;
    }
  }
}

std::list<ProduceFinishCbSharedPtr>& RichKafkaProducer::getUnfinishedRequestsForTest() {
  return unfinished_produce_requests_;
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
