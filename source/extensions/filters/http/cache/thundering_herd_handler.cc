#include "source/extensions/filters/http/cache/thundering_herd_handler.h"

#include <chrono>

#include "source/common/common/thread_annotations.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

class ThunderingHerdHandlerNone : public ThunderingHerdHandler {
public:
  void handleUpstreamRequest(std::weak_ptr<ThunderingHerdRetryInterface>,
                             Http::StreamDecoderFilterCallbacks* decoder_callbacks, const Key&,
                             Http::RequestHeaderMap&) override {
    decoder_callbacks->continueDecoding();
  }
  void handleInsertFinished(const Key&, bool) override {}
};

class ThunderingHerdHandlerBlockUntilCompletion
    : public ThunderingHerdHandler,
      public Logger::Loggable<Logger::Id::cache_filter>,
      public std::enable_shared_from_this<ThunderingHerdHandlerBlockUntilCompletion> {
  struct Queue {
    struct Blocked {
      std::weak_ptr<ThunderingHerdRetryInterface> weak_filter_;
      Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
      // dispatcher is captured separately from decoder_callbacks because decoder_callbacks
      // can be deleted before the dispatcher it returns.
      Event::Dispatcher& dispatcher_;
      // request_headers_ may be invalid if weak_filter_ is deleted.
      Http::RequestHeaderMap& request_headers_;
    };
    std::deque<Event::TimerPtr> blockers_;
    std::deque<Blocked> blocked_;
  };

public:
  ThunderingHerdHandlerBlockUntilCompletion(
      const envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler::
          BlockUntilCompletion& config)
      : unblock_additional_request_period_(
            PROTOBUF_GET_MS_OR_DEFAULT(config, unblock_additional_request_period, 0)),
        parallel_requests_(std::max(1U, config.parallel_requests())) {}

  ~ThunderingHerdHandlerBlockUntilCompletion() override {
    if (!queues_.empty()) {
      ENVOY_LOG(
          warn,
          fmt::format("thundering herd handler deleted while queues not empty ({} entries remain)",
                      queues_.size()));
    }
  }

  void handleUpstreamRequest(std::weak_ptr<ThunderingHerdRetryInterface> weak_filter,
                             Http::StreamDecoderFilterCallbacks* decoder_callbacks, const Key& key,
                             Http::RequestHeaderMap& request_headers) override {
    absl::MutexLock lock(&mu_);
    auto it = queues_.try_emplace(key).first;
    if (it->second.blockers_.size() < parallel_requests_) {
      ENVOY_LOG(debug, "ThunderingHerdHandler adding blocker {}",
                request_headers.Path()->value().getStringView());
      it->second.blockers_.push_back(maybeMakeTimeout(key, decoder_callbacks->dispatcher()));
      decoder_callbacks->continueDecoding();
    } else {
      ENVOY_LOG(debug, "ThunderingHerdHandler adding blocked {}",
                request_headers.Path()->value().getStringView());
      addBlocked(it->second, weak_filter, decoder_callbacks, request_headers);
    }
  }

  void addBlocked(Queue& queue, std::weak_ptr<ThunderingHerdRetryInterface> weak_filter,
                  Http::StreamDecoderFilterCallbacks* decoder_callbacks,
                  Http::RequestHeaderMap& request_headers) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    queue.blocked_.push_back(
        {weak_filter, decoder_callbacks, decoder_callbacks->dispatcher(), request_headers});
  }

  /**
   * Returns nullptr if unblock_additional_request_period was not configured.
   * Returns a timer that will expire after the configured duration if it was configured;
   * the timer will release one item from the queue.
   */
  Event::TimerPtr maybeMakeTimeout(const Key& key, Event::Dispatcher& dispatcher) {
    if (unblock_additional_request_period_ == std::chrono::milliseconds(0)) {
      return {};
    }
    Event::TimerPtr timer = dispatcher.createTimer(
        [handler = shared_from_this(), key]() { handler->selectNewBlocker(key); });
    timer->enableTimer(unblock_additional_request_period_);
    return timer;
  }

  std::function<void()> onUndeliverable(const Key& key) {
    return [key, handler = shared_from_this()]() { handler->handleInsertFinished(key, false); };
  }

  void selectNewBlocker(const Key& key, Queue& queue) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (queue.blocked_.empty()) {
      ENVOY_LOG(debug, "ThunderingHerdHandler ended blocker");
      if (queue.blockers_.empty()) {
        ENVOY_LOG(debug, "ThunderingHerdHandler deleted queue");
        queues_.erase(key);
      }
      return;
    }
    Queue::Blocked unblocking = std::move(queue.blocked_.front());
    queue.blocked_.pop_front();
    queue.blockers_.push_back(maybeMakeTimeout(key, unblocking.dispatcher_));
    unblocking.dispatcher_.post([unblocking, on_undeliverable = onUndeliverable(key)]() {
      ENVOY_LOG(debug, "ThunderingHerdHandler moving blocked to blocker");
      auto filter = unblocking.weak_filter_.lock();
      if (filter != nullptr) {
        unblocking.decoder_callbacks_->continueDecoding();
      } else {
        on_undeliverable();
      }
    });
  }

  void selectNewBlocker(const Key& key) ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    auto it = queues_.find(key);
    if (it == queues_.end()) {
      return;
    }
    selectNewBlocker(key, it->second);
  }

  void unblock(const Key& key, Queue& queue) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    ENVOY_LOG(debug, "ThunderingHerdHandler unblocking queue of size {}", queue.blocked_.size());
    for (const Queue::Blocked& unblocking : queue.blocked_) {
      unblocking.dispatcher_.post([unblocking]() {
        if (auto filter = unblocking.weak_filter_.lock()) {
          filter->retryHeaders(unblocking.request_headers_);
        }
      });
    }
    queue.blocked_.clear();
    if (queue.blockers_.empty()) {
      queues_.erase(key);
    }
  }

  void handleInsertFinished(const Key& key, bool write_succeeded) override {
    absl::MutexLock lock(&mu_);
    auto it = queues_.find(key);
    ASSERT(!write_succeeded || it != queues_.end(),
           "should be impossible to complete an insert on a queue that doesn't exist");
    if (it == queues_.end()) {
      // Ideally it would also not be possible to abort a queue entry that doesn't
      // exist, but a race is possible where a successful write clears the queue and
      // posts retryHeaders, then the filter is deleted before that post is delivered,
      // and the filter deletion therefore calls handleInsertFinished.
      // To accommodate this race we must simply ignore such a callback if it happens.
      return;
    }
    it->second.blockers_.pop_front();
    if (write_succeeded) {
      unblock(key, it->second);
    } else {
      selectNewBlocker(key, it->second);
    }
  }

private:
  absl::Mutex mu_;
  absl::flat_hash_map<Key, Queue, MessageUtil, MessageUtil> queues_ ABSL_GUARDED_BY(mu_);
  const std::chrono::milliseconds unblock_additional_request_period_;
  const size_t parallel_requests_;
};

// static
std::shared_ptr<ThunderingHerdHandler> ThunderingHerdHandler::create(
    const envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler& config) {
  switch (config.handler_case()) {
  case envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler::
      kBlockUntilCompletion:
    return std::make_shared<ThunderingHerdHandlerBlockUntilCompletion>(
        config.block_until_completion());
  case envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler::kNone:
  case envoy::extensions::filters::http::cache::v3::CacheConfig::ThunderingHerdHandler::
      HANDLER_NOT_SET:
    return std::make_shared<ThunderingHerdHandlerNone>();
  }
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
