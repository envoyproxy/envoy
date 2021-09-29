#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch.h"

#include <thread>

#include "absl/synchronization/mutex.h"
#include "contrib/kafka/filters/network/source/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

FetchRequestHolder::FetchRequestHolder(AbstractRequestListener& filter,
                                       RecordCallbackProcessor& consumer_manager,
                                       const std::shared_ptr<Request<FetchRequest>> request)
    : BaseInFlightRequest{filter}, consumer_manager_{consumer_manager}, request_{request},
      dispatcher_{filter.dispatcher()} {}

FetchRequestHolder::~FetchRequestHolder() { ENVOY_LOG(info, "Fetch {} DTOR", toString()); }

// XXX (adam.kotwasinski) This should be made configurable in future.
constexpr uint32_t FETCH_TIMEOUT_MS = 5000;

static Event::TimerPtr registerTimeoutCallback(Event::Dispatcher& dispatcher,
                                               Event::TimerCb callback, int32_t timeout) {
  auto event = dispatcher.createTimer(callback);
  event->enableTimer(std::chrono::milliseconds(timeout));
  return event;
}

void FetchRequestHolder::startProcessing() {
  const TopicToPartitionsMap requested_topics = interest();

  {
    absl::MutexLock lock(&state_mutex_);
    for (const auto& topic_and_partitions : requested_topics) {
      const std::string& topic_name = topic_and_partitions.first;
      for (const int32_t partition : topic_and_partitions.second) {
        // This makes sure that all requested KafkaPartitions are tracked,
        // so then output generation is simpler.
        messages_[{topic_name, partition}] = {};
      }
    }
  }

  const auto self_reference = shared_from_this();
  consumer_manager_.processCallback(self_reference);

  // Event::TimerCb callback = [self_reference]() -> void {
  Event::TimerCb callback = [this]() -> void {
    // Fun fact: if the request is degenerate (no partitions requested), this will make it be
    // processed. self_reference->markFinishedByTimer();
    markFinishedByTimer();
  };
  timer_ = registerTimeoutCallback(dispatcher_, callback, FETCH_TIMEOUT_MS);
}

TopicToPartitionsMap FetchRequestHolder::interest() const {
  TopicToPartitionsMap result;
  const std::vector<FetchTopic>& topics = request_->data_.topics_;
  for (const FetchTopic& topic : topics) {
    const std::string topic_name = topic.topic_;
    const std::vector<FetchPartition> partitions = topic.partitions_;
    for (const FetchPartition& partition : partitions) {
      result[topic_name].push_back(partition.partition_);
    }
  }
  return result;
}

// This method is called by a Envoy-worker thread.
void FetchRequestHolder::markFinishedByTimer() {
  ENVOY_LOG(info, "Fetch request {} timed out", toString());
  bool doCleanup = false;
  {
    absl::MutexLock lock(&state_mutex_);
    timer_ = nullptr;
    if (!finished_) {
      finished_ = true;
      doCleanup = true;
    }
  }
  if (doCleanup) {
    cleanup(true);
  }
}

// XXX temporary solution only
constexpr int32_t MINIMAL_MSG_CNT = 3;

// This method is called by:
// - Kafka-consumer thread - when have the records delivered,
// - dispatcher thread  - when we start processing and check whether anything was cached.
CallbackReply FetchRequestHolder::receive(InboundRecordSharedPtr message) {
  absl::MutexLock lock(&state_mutex_);
  if (!finished_) {
    const KafkaPartition kp = {message->topic_, message->partition_};
    messages_[kp].push_back(message);

    uint32_t current_messages = 0;
    for (const auto& e : messages_) {
      current_messages += e.second.size();
    }

    if (current_messages < MINIMAL_MSG_CNT) {
      ENVOY_LOG(info, "Fetch request {} processed message (and wants more {}): {}", toString(),
                current_messages, message->toString());
      return CallbackReply::AcceptedAndWantMore;
    } else {
      ENVOY_LOG(info, "Fetch request {} processed message (and is finished with {}): {}",
                toString(), current_messages, message->toString());
      // We have all we needed, we can finish processing.
      finished_ = true;
      cleanup(false);
      return CallbackReply::AcceptedAndFinished;
    }
  } else {
    ENVOY_LOG(info, "Fetch request {} rejected message: {}", toString(), message->toString());
    return CallbackReply::Rejected;
  }
}

std::string FetchRequestHolder::toString() const {
  std::ostringstream oss;
  oss << "["
      << "?"
      << "/" << request_->request_header_.correlation_id_ << "]";
  return oss.str();
}

void FetchRequestHolder::cleanup(bool unregister) {
  ENVOY_LOG(info, "Cleanup starting for {}", toString());
  if (unregister) {
    const auto self_reference = shared_from_this();
    consumer_manager_.removeCallback(self_reference);
  }

  // Our request is ready and can be sent downstream.
  // However, the caller here could be a Kafka-consumer worker thread (not an Envoy worker one),
  // so we need to use dispatcher to notify the filter that we are finished.
  auto notifyCallback = [this]() -> void {
    timer_ = nullptr;
    filter_.onRequestReadyForAnswer();
  };
  // Impl note: usually this will be invoked by non-Envoy thread,
  // so let's not optimize that this might be invoked by dispatcher callback.
  dispatcher_.post(notifyCallback);
  ENVOY_LOG(info, "Cleanup finished for {}", toString());
}

bool FetchRequestHolder::finished() const {
  absl::MutexLock lock(&state_mutex_);
  return finished_;
}

void FetchRequestHolder::abandon() {
  // We remove the timeout-callback and unregister this request so no deliveries happen to it.
  timer_ = nullptr;
  const auto self_reference = shared_from_this();
  consumer_manager_.removeCallback(self_reference);
  BaseInFlightRequest::abandon();
  // XXX (adam.kotwasinski) Might want to "push-back" records that are already in this request.
  // Replication path: make a Fetch that wants 10 records, provide 8, kill connection.
  // Part 2: might escalate even higher, what if we finish okay, but cannot send serialized form?
}

AbstractResponseSharedPtr FetchRequestHolder::computeAnswer() const {
  const auto& header = request_->request_header_;
  const ResponseMetadata metadata = {header.api_key_, header.api_version_, header.correlation_id_};

  const int32_t throttle_time_ms = 0;
  std::vector<FetchableTopicResponse> responses;
  {
    absl::MutexLock lock(&state_mutex_);
    responses = processor_.transform(messages_);
  }
  const FetchResponse data = {throttle_time_ms, responses};
  return std::make_shared<Response<FetchResponse>>(metadata, data);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
