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

RecordDistributor::RecordDistributor() : RecordDistributor({}, {}){};

RecordDistributor::RecordDistributor(const PartitionMap<RecordCbSharedPtr>& callbacks,
                                     const PartitionMap<InboundRecordSharedPtr>& records)
    : partition_to_callbacks_{callbacks}, stored_records_{records} {};

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

// XXX (adam.kotwasinski) Inefficient: locks acquired per record.
void RecordDistributor::receive(InboundRecordSharedPtr record) {

  const KafkaPartition kafka_partition = {record->topic_, record->partition_};

  // Whether this record has been consumed by any of the callbacks.
  // Because then we can safely throw it away instead of storing.
  bool consumed_by_callback = false;

  {
    absl::MutexLock lock(&callbacks_mutex_);
    auto& callbacks = partition_to_callbacks_[kafka_partition];

    std::vector<RecordCbSharedPtr> satisfied_callbacks = {};

    // Typical case: there is some interest in records for given partition.
    // Notify the callback and remove it.
    for (const auto& callback : callbacks) {
      CallbackReply callback_status = callback->receive(record);
      switch (callback_status) {
      case CallbackReply::AcceptedAndFinished: {
        consumed_by_callback = true;
        // A callback is finally satisfied, it will never want more records.
        satisfied_callbacks.push_back(callback);
        break;
      }
      case CallbackReply::AcceptedAndWantMore: {
        consumed_by_callback = true;
        break;
      }
      case CallbackReply::Rejected: {
        break;
      }
      } /* switch */

      /* Some callback has taken the record - this is good, no more iterating. */
      if (consumed_by_callback) {
        break;
      }
    }

    for (const auto& callback : satisfied_callbacks) {
      doRemoveCallback(callback);
    }
  }

  // No-one is interested in our record, so we are going to store it in a local cache.
  if (!consumed_by_callback) {
    absl::MutexLock lock(&stored_records_mutex_);
    auto& stored_records = stored_records_[kafka_partition];
    // XXX (adam.kotwasinski) Implement some kind of limit.
    stored_records.push_back(record);
    ENVOY_LOG(trace, "Stored record [{}]", record->toString());
  }
}

void RecordDistributor::processCallback(const RecordCbSharedPtr& callback) {
  ENVOY_LOG(trace, "Processing callback {}", callback->toString());

  // Attempt to fulfill callback's requirements using the stored records.
  bool fulfilled_at_startup = passRecordsToCallback(callback);

  if (fulfilled_at_startup) {
    // Early exit: callback was fulfilled with only stored records.
    // What means it will not require anything anymore, and does not need to be registered.
    ENVOY_LOG(trace, "No registration for callback {} due to successful early processing",
              callback->toString());
    return;
  }

  // Usual path: the request was not fulfilled at receive time (there were no stored messages).
  // So we just register the callback.
  TopicToPartitionsMap requested = callback->interest();
  absl::MutexLock lock(&callbacks_mutex_);
  for (const auto& topic_and_partitions : requested) {
    const std::string topic = topic_and_partitions.first;
    for (const int32_t partition : topic_and_partitions.second) {
      const KafkaPartition kp = {topic, partition};
      auto& partition_callbacks = partition_to_callbacks_[kp];
      partition_callbacks.push_back(callback);
    }
  }
}

bool RecordDistributor::passRecordsToCallback(const RecordCbSharedPtr& callback) {
  TopicToPartitionsMap requested = callback->interest();
  absl::MutexLock lock(&stored_records_mutex_);

  for (const auto& topic_and_partitions : requested) {
    for (const int32_t partition : topic_and_partitions.second) {
      const KafkaPartition kp = {topic_and_partitions.first, partition};
      // Processing of given partition's records was enough for given callback.
      const bool processing_finished = passPartitionRecordsToCallback(callback, kp);
      if (processing_finished) {
        return true;
      }
    }
  }

  // All the eligible records have been passed to callback, but it still wants more.
  // So we are going to need to register it.
  return false;
}

bool RecordDistributor::passPartitionRecordsToCallback(const RecordCbSharedPtr& callback,
                                                       const KafkaPartition& kafka_partition) {
  const auto it = stored_records_.find(kafka_partition);
  if (stored_records_.end() == it) {
    // This partition does not have any records buffered.
    return false;
  }

  auto& partition_records = it->second;
  ENVOY_LOG(trace, "Early notification for callback {}, as there are {} messages available",
            callback->toString(), partition_records.size());

  bool processing_finished = false;
  for (auto record_it = partition_records.begin(); record_it != partition_records.end();) {
    const CallbackReply callback_status = callback->receive(*record_it);
    switch (callback_status) {
    case CallbackReply::AcceptedAndWantMore: {
      // Callback consumed the record, and wants more. We keep iterating.
      record_it = partition_records.erase(record_it);
      break;
    }
    case CallbackReply::AcceptedAndFinished: {
      // We had a callback that wanted records, and got all it wanted in the initial
      // processing (== everything it needed was buffered), so we won't need to register it.
      record_it = partition_records.erase(record_it);
      processing_finished = true;
      break;
    }
    case CallbackReply::Rejected: {
      // Our callback entered a terminal state in the meantime. We won't work with it anymore.
      processing_finished = true;
      break;
    }
    } /* switch */

    if (processing_finished) {
      // No more processing needed.
      break;
    }
  } /* for-stored-records */

  if (partition_records.empty()) {
    // The partition's buffer got drained - there is no reason to keep empty vectors.
    stored_records_.erase(it);
  }

  return processing_finished;
}

void RecordDistributor::removeCallback(const RecordCbSharedPtr& callback) {
  absl::MutexLock lock(&callbacks_mutex_);
  doRemoveCallback(callback);
}

void RecordDistributor::doRemoveCallback(const RecordCbSharedPtr& callback) {
  ENVOY_LOG(trace, "Removing callback {}", callback->toString());
  for (auto it = partition_to_callbacks_.begin(); it != partition_to_callbacks_.end();) {
    auto& partition_callbacks = it->second;
    partition_callbacks.erase(
        std::remove(partition_callbacks.begin(), partition_callbacks.end(), callback),
        partition_callbacks.end());
    if (partition_callbacks.empty()) {
      it = partition_to_callbacks_.erase(it);
    } else {
      ++it;
    }
  }
}

// Just a helper function for tests.
template <typename T>
int32_t countForTest(const std::string& topic, const int32_t partition, PartitionMap<T> map) {
  const auto it = map.find({topic, partition});
  if (map.end() != it) {
    return it->second.size();
  } else {
    return -1; // Tests are simpler to type if we do this instead of absl::optional.
  }
}

int32_t RecordDistributor::getCallbackCountForTest(const std::string& topic,
                                                   const int32_t partition) const {
  absl::MutexLock lock(&callbacks_mutex_);
  return countForTest(topic, partition, partition_to_callbacks_);
}

int32_t RecordDistributor::getRecordCountForTest(const std::string& topic,
                                                 const int32_t partition) const {
  absl::MutexLock lock(&stored_records_mutex_);
  return countForTest(topic, partition, stored_records_);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
