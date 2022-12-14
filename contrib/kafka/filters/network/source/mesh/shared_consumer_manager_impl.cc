#include "contrib/kafka/filters/network/source/mesh/shared_consumer_manager_impl.h"

#include <functional>

#include "source/common/common/fmt.h"

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_consumer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// KafkaConsumerFactoryImpl

class KafkaConsumerFactoryImpl : public KafkaConsumerFactory {
public:
  // KafkaConsumerFactory
  KafkaConsumerPtr createConsumer(InboundRecordProcessor& record_processor,
                                  Thread::ThreadFactory& thread_factory, const std::string& topic,
                                  const int32_t partition_count,
                                  const RawKafkaConfig& configuration) const override;

  // Default singleton accessor.
  static const KafkaConsumerFactory& getDefaultInstance();
};

KafkaConsumerPtr
KafkaConsumerFactoryImpl::createConsumer(InboundRecordProcessor& record_processor,
                                         Thread::ThreadFactory& thread_factory,
                                         const std::string& topic, const int32_t partition_count,
                                         const RawKafkaConfig& configuration) const {
  return std::make_unique<RichKafkaConsumer>(record_processor, thread_factory, topic,
                                             partition_count, configuration);
}

const KafkaConsumerFactory& KafkaConsumerFactoryImpl::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(KafkaConsumerFactoryImpl);
}

// SharedConsumerManagerImpl

SharedConsumerManagerImpl::SharedConsumerManagerImpl(
    const UpstreamKafkaConfiguration& configuration, Thread::ThreadFactory& thread_factory)
    : SharedConsumerManagerImpl{configuration, thread_factory,
                                KafkaConsumerFactoryImpl::getDefaultInstance()} {}

SharedConsumerManagerImpl::SharedConsumerManagerImpl(
    const UpstreamKafkaConfiguration& configuration, Thread::ThreadFactory& thread_factory,
    const KafkaConsumerFactory& consumer_factory)
    : distributor_{std::make_unique<RecordDistributor>()}, configuration_{configuration},
      thread_factory_{thread_factory}, consumer_factory_{consumer_factory} {}

void SharedConsumerManagerImpl::processCallback(const RecordCbSharedPtr& callback) {

  // For every fetch topic, figure out the upstream cluster,
  // create consumer if needed ...
  const TopicToPartitionsMap interest = callback->interest();
  for (const auto& fetch : interest) {
    const std::string& topic = fetch.first;
    registerConsumerIfAbsent(topic);
  }

  // ... and start processing.
  distributor_->processCallback(callback);
}

void SharedConsumerManagerImpl::removeCallback(const RecordCbSharedPtr& callback) {
  // Real work - let's remove the callback.
  distributor_->removeCallback(callback);
}

void SharedConsumerManagerImpl::registerConsumerIfAbsent(const std::string& topic) {
  absl::MutexLock lock(&consumers_mutex_);
  const auto it = topic_to_consumer_.find(topic);
  // Return consumer already present or create new one and register it.
  if (topic_to_consumer_.end() == it) {
    registerNewConsumer(topic);
  }
}

void SharedConsumerManagerImpl::registerNewConsumer(const std::string& topic) {
  ENVOY_LOG(debug, "Creating consumer for topic [{}]", topic);

  // Compute which upstream cluster corresponds to the topic.
  const absl::optional<ClusterConfig> cluster_config =
      configuration_.computeClusterConfigForTopic(topic);
  if (!cluster_config) {
    throw EnvoyException(
        fmt::format("Could not compute upstream cluster configuration for topic [{}]", topic));
  }

  // Create the consumer and register it.
  KafkaConsumerPtr new_consumer = consumer_factory_.createConsumer(
      *distributor_, thread_factory_, topic, cluster_config->partition_count_,
      cluster_config->upstream_consumer_properties_);
  ENVOY_LOG(debug, "Registering new Kafka consumer for topic [{}], consuming from cluster [{}]",
            topic, cluster_config->name_);
  topic_to_consumer_.emplace(topic, std::move(new_consumer));
}

size_t SharedConsumerManagerImpl::getConsumerCountForTest() const {
  absl::MutexLock lock(&consumers_mutex_);
  return topic_to_consumer_.size();
}

// RecordDistributor

bool RecordDistributor::waitUntilInterest(const std::string& topic,
                                          const int32_t timeout_ms) const {

  auto distributor_has_interest = std::bind(&RecordDistributor::hasInterest, this, topic);
  // Effectively this means "has an interest appeared within timeout".
  // If not, we let the user know so they could do something else
  // instead of being infinitely blocked.
  bool can_poll = callbacks_mutex_.LockWhenWithTimeout(absl::Condition(&distributor_has_interest),
                                                       absl::Milliseconds(timeout_ms));
  callbacks_mutex_.Unlock(); // Lock...WithTimeout always locks, so we need to unlock.
  return can_poll;
}

bool RecordDistributor::hasInterest(const std::string& topic) const {
  for (const auto& e : partition_to_callbacks_) {
    if (topic == e.first.first && !e.second.empty()) {
      return true;
    }
  }
  return false;
}

// XXX (adam.kotwasinski) Inefficient: locks aquired per message.
void RecordDistributor::receive(InboundRecordSharedPtr message) {

  const KafkaPartition kafka_partition = {message->topic_, message->partition_};

  // Whether this message has been consumed by any of the callbacks.
  // Because then we can safely throw it away instead of storing.
  bool consumed_by_callback = false;

  {
    absl::MutexLock lock(&callbacks_mutex_);
    auto& callbacks = partition_to_callbacks_[kafka_partition];

    std::vector<RecordCbSharedPtr> satisfied_callbacks = {};

    // Typical case: there is some interest in messages for given partition. Notify the callback and
    // remove it.
    for (const auto& callback : callbacks) {
      CallbackReply callback_status = callback->receive(message);
      switch (callback_status) {
      case CallbackReply::ACCEPTED_AND_FINISHED: {
        consumed_by_callback = true;
        // A callback is finally satisfied, it will never want more messages.
        satisfied_callbacks.push_back(callback);
        break;
      }
      case CallbackReply::ACCEPTED_AND_WANT_MORE: {
        consumed_by_callback = true;
        break;
      }
      case CallbackReply::REJECTED: {
        break;
      }
      } /* switch */

      /* Some callback has taken the message - this is good, no more iterating. */
      if (consumed_by_callback) {
        break;
      }
    }

    for (const auto& callback : satisfied_callbacks) {
      doRemoveCallback(callback);
    }
  }

  // Noone is interested in our message, so we are going to store it in a local cache.
  if (!consumed_by_callback) {
    absl::MutexLock lock(&messages_mutex_);
    auto& stored_messages = messages_waiting_for_interest_[kafka_partition];
    // XXX (adam.kotwasinski) Implement some kind of limit.
    stored_messages.push_back(message);
    ENVOY_LOG(trace, "Stored message [{}]", message->toString());
  }
}

void RecordDistributor::processCallback(const RecordCbSharedPtr& callback) {

  TopicToPartitionsMap requested = callback->interest();
  ENVOY_LOG(trace, "Processing callback {}", callback->toString());

  bool fulfilled_at_startup = false;

  {
    absl::MutexLock lock(&messages_mutex_);
    for (const auto& topic_and_partitions : requested) {
      const std::string topic = topic_and_partitions.first;
      for (const int32_t partition : topic_and_partitions.second) {
        const KafkaPartition kp = {topic, partition};

        auto& stored_messages = messages_waiting_for_interest_[kp];
        if (0 != stored_messages.size()) {
          ENVOY_LOG(trace, "Early notification for callback {}, as there are {} messages available",
                    callback->toString(), stored_messages.size());
        }

        for (auto it = stored_messages.begin(); it != stored_messages.end();) {
          CallbackReply callback_status = callback->receive(*it);
          bool callback_finished;
          switch (callback_status) {
          case CallbackReply::ACCEPTED_AND_FINISHED: {
            // We had a callback that wanted records, and got all it wanted in the initial
            // processing (== everything it needed was buffered), so we won't need to register it.
            callback_finished = true;
            it = stored_messages.erase(it);
            break;
          }
          case CallbackReply::ACCEPTED_AND_WANT_MORE: {
            callback_finished = false;
            it = stored_messages.erase(it);
            break;
          }
          case CallbackReply::REJECTED: {
            callback_finished = true;
            break;
          }
          } /* switch */
          if (callback_finished) {
            fulfilled_at_startup = true;
            break; // Callback does not want any messages anymore.
          }
        } /* for-messages */
      }   /* for-partitions */
    }     /* for-topic_and_partitions */
  }       /* lock on messages_mutex_ */

  if (!fulfilled_at_startup) {
    // Usual path: the request was not fulfilled at receive time (there were no buffered messages).
    // So we just register the callback.
    absl::MutexLock lock(&callbacks_mutex_);

    for (const auto& topic_and_partitions : requested) {
      const std::string topic = topic_and_partitions.first;
      for (const int32_t partition : topic_and_partitions.second) {
        const KafkaPartition kp = {topic, partition};
        auto& partition_callbacks = partition_to_callbacks_[kp];
        partition_callbacks.push_back(callback);
      }
    }
  } else {
    ENVOY_LOG(trace, "No registration for callback {} due to successful early processing",
              callback->toString());
  }
}

void RecordDistributor::removeCallback(const RecordCbSharedPtr& callback) {
  absl::MutexLock lock(&callbacks_mutex_);
  doRemoveCallback(callback);
}

void RecordDistributor::doRemoveCallback(const RecordCbSharedPtr& callback) {
  ENVOY_LOG(trace, "Removing callback {}", callback->toString());
  for (auto& e : partition_to_callbacks_) {
    auto& partition_callbacks = e.second;
    partition_callbacks.erase(
        std::remove(partition_callbacks.begin(), partition_callbacks.end(), callback),
        partition_callbacks.end());
  }
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
