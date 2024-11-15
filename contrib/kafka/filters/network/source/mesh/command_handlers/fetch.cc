#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch.h"

#include <thread>

#include "source/common/common/fmt.h"

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
    : FetchRequestHolder{filter, consumer_manager, request,
                         FetchRecordConverterImpl::getDefaultInstance()} {}

FetchRequestHolder::FetchRequestHolder(AbstractRequestListener& filter,
                                       RecordCallbackProcessor& consumer_manager,
                                       const std::shared_ptr<Request<FetchRequest>> request,
                                       const FetchRecordConverter& converter)
    : BaseInFlightRequest{filter}, consumer_manager_{consumer_manager}, request_{request},
      dispatcher_{filter.dispatcher()}, converter_{converter} {}

// XXX (adam.kotwasinski) This should be made configurable in future.
constexpr uint32_t FETCH_TIMEOUT_MS = 5000;

static Event::TimerPtr registerTimeoutCallback(Event::Dispatcher& dispatcher,
                                               const Event::TimerCb callback,
                                               const int32_t timeout) {
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

  Event::TimerCb callback = [this]() -> void {
    // Fun fact: if the request is degenerate (no partitions requested),
    // this will ensure it gets processed.
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
  ENVOY_LOG(trace, "Request {} timed out", toString());
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

// XXX (adam.kotwasinski) This should be made configurable in future.
// Right now the Fetch request is going to send up to 3 records.
// In future this should transform into some kind of method that's invoked inside 'receive' calls,
// as Kafka can have limits on records per partition.
constexpr int32_t MINIMAL_MSG_CNT = 3;

// This method is called by:
// - Kafka-consumer thread - when have the records delivered,
// - dispatcher thread  - when we start processing and check whether anything was cached.
CallbackReply FetchRequestHolder::receive(InboundRecordSharedPtr message) {
  absl::MutexLock lock(&state_mutex_);
  if (!finished_) {
    // Store a new record.
    const KafkaPartition kp = {message->topic_, message->partition_};
    messages_[kp].push_back(message);

    // Count all the records currently stored within this request.
    uint32_t current_messages = 0;
    for (const auto& e : messages_) {
      current_messages += e.second.size();
    }

    if (current_messages < MINIMAL_MSG_CNT) {
      // We can consume more in future.
      return CallbackReply::AcceptedAndWantMore;
    } else {
      // We have all we needed, we can finish processing.
      finished_ = true;
      cleanup(false);
      return CallbackReply::AcceptedAndFinished;
    }
  } else {
    // This fetch request has finished processing, so it will not accept a record.
    return CallbackReply::Rejected;
  }
}

std::string FetchRequestHolder::toString() const {
  return fmt::format("[Fetch id={}]", request_->request_header_.correlation_id_);
}

void FetchRequestHolder::cleanup(bool unregister) {
  ENVOY_LOG(trace, "Cleanup starting for {}", toString());
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
  ENVOY_LOG(trace, "Cleanup finished for {}", toString());
}

bool FetchRequestHolder::finished() const {
  absl::MutexLock lock(&state_mutex_);
  return finished_;
}

void FetchRequestHolder::abandon() {
  ENVOY_LOG(trace, "Abandoning {}", toString());
  // We remove the timeout-callback and unregister this request so no deliveries happen to it.
  timer_ = nullptr;
  const auto self_reference = shared_from_this();
  consumer_manager_.removeCallback(self_reference);
  BaseInFlightRequest::abandon();
}

AbstractResponseSharedPtr FetchRequestHolder::computeAnswer() const {
  const auto& header = request_->request_header_;
  const ResponseMetadata metadata = {header.api_key_, header.api_version_, header.correlation_id_};

  std::vector<FetchableTopicResponse> responses;
  {
    absl::MutexLock lock(&state_mutex_);
    responses = converter_.convert(messages_);
  }
  const FetchResponse data = {responses};
  return std::make_shared<Response<FetchResponse>>(metadata, data);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
