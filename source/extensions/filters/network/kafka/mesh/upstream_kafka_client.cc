#include "extensions/filters/network/kafka/mesh/upstream_kafka_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class LibRdKafkaUtilsImpl : public LibRdKafkaUtils {
  RdKafka::Conf::ConfResult setConfProperty(RdKafka::Conf& conf, const std::string& name,
                                            const std::string& value,
                                            std::string& errstr) const override {
    return conf.set(name, value, errstr);
  }

  RdKafka::Conf::ConfResult setConfDeliveryCallback(RdKafka::Conf& conf,
                                                    RdKafka::DeliveryReportCb* dr_cb,
                                                    std::string& errstr) const override {
    return conf.set("dr_cb", dr_cb, errstr);
  }

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

  std::unique_ptr<RdKafka::Conf> conf =
      std::unique_ptr<RdKafka::Conf>(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
  std::string errstr;
  for (const auto& e : configuration) {
    if (utils.setConfProperty(*conf, e.first, e.second, errstr) != RdKafka::Conf::CONF_OK) {
      throw EnvoyException(absl::StrCat("Could not set producer property [", e.first, "] to [",
                                        e.second, "]:", errstr));
    }
  }

  if (utils.setConfDeliveryCallback(*conf, this, errstr) != RdKafka::Conf::CONF_OK) {
    ENVOY_LOG(warn, "dr_cb", errstr);
    throw EnvoyException(absl::StrCat("Could not set producer callback:", errstr));
  }

  producer_ = utils.createProducer(conf.get(), errstr);
  if (!producer_) {
    throw EnvoyException(absl::StrCat("Could not create producer:", errstr));
  }

  // Here we are starting the monitoring thread.
  poller_thread_active_ = true;
  std::function<void()> thread_routine = [this]() -> void { checkDeliveryReports(); };
  poller_thread_ = thread_factory.createThread(thread_routine);
}

RichKafkaProducer::~RichKafkaProducer() {
  ENVOY_LOG(warn, "Shutting down worker thread");
  // Impl note: this could be optimized by having flag flipped by owning Facade for all of its
  // clients so shutdowns happen in parallel.
  poller_thread_active_ = false;
  poller_thread_->join();
  ENVOY_LOG(warn, "Worker thread shut down successfully");
}

void RichKafkaProducer::send(const ProduceFinishCbSharedPtr origin, const std::string& topic,
                             const int32_t partition, const absl::string_view key,
                             const absl::string_view value) {
  {
    ENVOY_LOG(warn, "Sending {} value-bytes to [{}/{}] (data = {})", value.size(), topic, partition,
              reinterpret_cast<long>(value.data()));
    // No flags, we leave all the memory management to Envoy, as we will use address of value data
    // in message callback to tell apart the requests.
    const int flags = 0;
    void* value_data = const_cast<char*>(value.data());
    RdKafka::ErrorCode ec = producer_->produce(topic, partition, flags, value_data, value.size(),
                                               key.data(), key.size(), 0, nullptr);
    if (RdKafka::ERR_NO_ERROR == ec) {
      // We have succeeded with submitting data to producer, so we register a callback.
      unfinished_produce_requests_.push_back(origin);
    } else {
      ENVOY_LOG(warn, "Produce failure [{}] while sending {} value-bytes to [{}/{}]", ec,
                value.size(), topic, partition);
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
  ENVOY_LOG(warn, "Poller thread finished");
}

// Kafka callback that contains the information
void RichKafkaProducer::dr_cb(RdKafka::Message& message) {
  ENVOY_LOG(warn, "RichKafkaProducer - dr_cb: [{}] {}/{} -> {} (data = {})", message.err(),
            message.topic_name(), message.partition(), message.offset(),
            reinterpret_cast<long>(message.payload()));
  const DeliveryMemento memento = {message.payload(), message.err(), message.offset()};
  const Event::PostCb callback = [this, memento]() -> void { processDelivery(memento); };
  dispatcher_.post(callback);
}

void RichKafkaProducer::processDelivery(const DeliveryMemento& memento) {
  ENVOY_LOG(trace, "RichKafkaProducer - processDelivery - entry [{}]",
            reinterpret_cast<long>(memento.data_));
  for (auto it = unfinished_produce_requests_.begin(); it != unfinished_produce_requests_.end();) {
    bool accepted = (*it)->accept(memento);
    if (accepted) {
      ENVOY_LOG(trace, "RichKafkaProducer - processDelivery - accepted [{}]",
                reinterpret_cast<long>(memento.data_));
      unfinished_produce_requests_.erase(it);
      break; // This is important - a single request can be mapped into multiple callbacks here.
    } else {
      ++it;
    }
  }
}

void RichKafkaProducer::markFinished() { poller_thread_active_ = false; }

std::list<ProduceFinishCbSharedPtr>& RichKafkaProducer::getUnfinishedRequestsForTest() {
  return unfinished_produce_requests_;
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
