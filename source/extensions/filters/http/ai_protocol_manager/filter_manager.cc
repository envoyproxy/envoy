#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

struct FilterManager::AsyncState : public std::enable_shared_from_this<FilterManager::AsyncState>,
                                   public Logger::Loggable<Logger::Id::ai_protocol_manager> {
  struct FilterContext {
    std::shared_ptr<Coroutine::AsyncQueue<AiRequestPtr>> handoff;
    bool received{false};
    bool propagated{false};
  };

  AsyncState(size_t num_filters, BufferManager* buffer_manager, StreamInfo::StreamInfo& stream_info,
             std::shared_ptr<Coroutine::DispatcherExecutor> executor, LocalReplyFn local_reply_fn)
      : buffer_manager_(buffer_manager), stream_info_(stream_info), executor_(std::move(executor)),
        local_reply_fn_(std::move(local_reply_fn)) {
    // num_filters + 1 stages: 0..N-1 are filters, N is the sink.
    for (size_t i = 0; i <= num_filters; ++i) {
      filter_contexts_.push_back({
          std::make_shared<Coroutine::AsyncQueue<AiRequestPtr>>(/*max_size=*/1),
          /*received=*/false,
          /*propagated=*/false,
      });
    }
  }

  Coroutine::Task<absl::StatusOr<AiRequestPtr>> receiveRequest(size_t index) {
    ASSIGN_OR_CO_RETURN(auto res, co_await filter_contexts_[index].handoff->pop());
    if (!res.has_value()) {
      co_return absl::InternalError("request handoff queue closed unexpectedly");
    }
    filter_contexts_[index].received = true;
    co_return std::move(*res);
  }

  Coroutine::Task<absl::Status> bypassFilter(size_t index) {
    ASSIGN_OR_CO_RETURN(auto res, co_await filter_contexts_[index].handoff->pop());
    if (!res.has_value()) {
      co_return absl::InternalError("request handoff queue closed unexpectedly");
    }
    co_return co_await propagateRequest(index, std::move(*res));
  }

  Coroutine::Task<absl::Status> propagateRequest(size_t index, AiRequestPtr req) {
    if (req == nullptr) {
      IS_ENVOY_BUG("cannot propagate null AiRequestPtr");
      co_return absl::InvalidArgumentError("cannot propagate null AiRequestPtr");
    }
    filter_contexts_[index].propagated = true;
    co_return co_await filter_contexts_[index + 1].handoff->push(std::move(req));
  }

  Coroutine::Task<absl::Status> runSink() {
    ASSIGN_OR_CO_RETURN(auto res, co_await filter_contexts_.back().handoff->pop());
    if (!res.has_value()) {
      co_return absl::InternalError("sink handoff queue closed unexpectedly");
    }

    final_req_ = std::move(*res);
    ASSERT(final_req_ != nullptr);

    ASSIGN_OR_CO_RETURN(
        auto new_doc, co_await Serializer::calculateSerializedOffsets(final_req_->request_index()));

    if (stream_info_.filterState() != nullptr) {
      stream_info_.filterState()->setData(
          APMRequestPayloadIndex::kFilterStateKey,
          std::make_shared<APMRequestPayloadIndex>(std::move(new_doc)),
          StreamInfo::FilterState::LifeSpan::Request);
    }

    ASSIGN_OR_CO_RETURN(
        std::ignore, co_await Serializer::serialize(final_req_->request_index(), buffer_manager_));

    terminated_ = true;
    if (on_complete_ != nullptr) {
      auto cb = std::move(on_complete_);
      on_complete_ = nullptr;
      cb(absl::OkStatus());
    }
    co_return absl::OkStatus();
  }

  void onFilterCompletion(size_t index, absl::Status status) {
    if (terminated_) {
      return;
    }
    if (!status.ok()) {
      onFilterError(std::move(status));
      return;
    }

    if (filter_contexts_[index].propagated) {
      return;
    }

    if (!filter_contexts_[index].received) {
      // Filter early-returned without calling receive_request (bypassed itself).
      // Forward the request in its handoff queue directly to the next stage.
      std::weak_ptr<AsyncState> weak_self = shared_from_this();
      auto handle = Coroutine::launch(
          bypassFilter(index), executor_,
          [weak_self, index](absl::Status status) {
            if (auto self = weak_self.lock()) {
              self->onFilterCompletion(index, std::move(status));
            }
          },
          Coroutine::StartMode::Inline);
      if (!terminated_) {
        handles_.push_back(std::move(handle));
      }
      return;
    }

    // Filter called receive_request, but completed without calling propagateRequest or
    // reply_locally.
    onFilterError(absl::InternalError(
        "filter consumed request but terminated without propagating or replying locally"));
  }

  void onFilterError(absl::Status status) {
    if (terminated_) {
      return;
    }
    cancel();
    ENVOY_LOG(debug, "ai_protocol_manager: filter chain error: {}", status.message());
    auto reply_fn = std::move(local_reply_fn_);
    local_reply_fn_ = nullptr;
    auto on_complete = std::move(on_complete_);
    on_complete_ = nullptr;

    if (reply_fn != nullptr) {
      reply_fn(Http::Code::BadRequest, std::string(status.message()));
    }
    if (on_complete != nullptr) {
      on_complete(std::move(status));
    }
  }

  void triggerLocalReply(Http::Code code, std::string details) {
    if (terminated_) {
      return;
    }
    cancel();
    ENVOY_LOG(debug, "ai_protocol_manager: filter chain triggered local reply: {} {}",
              static_cast<uint32_t>(code), details);
    auto reply_fn = std::move(local_reply_fn_);
    local_reply_fn_ = nullptr;
    auto on_complete = std::move(on_complete_);
    on_complete_ = nullptr;

    if (reply_fn != nullptr) {
      reply_fn(code, std::move(details));
    }
    if (on_complete != nullptr) {
      on_complete(absl::CancelledError("local reply sent"));
    }
  }

  void cancel() {
    if (terminated_) {
      return;
    }
    terminated_ = true;
    for (auto& handle : handles_) {
      handle.cancel();
    }
    handles_.clear();
    for (auto& ctx : filter_contexts_) {
      ctx.handoff->close();
    }
  }

  BufferManager* buffer_manager_{nullptr};
  StreamInfo::StreamInfo& stream_info_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  LocalReplyFn local_reply_fn_;
  absl::AnyInvocable<void(absl::Status)> on_complete_;
  AiRequestPtr final_req_;
  std::vector<FilterContext> filter_contexts_;
  std::vector<Coroutine::DetachedHandle> handles_;
  bool terminated_{false};
};

FilterManager::FilterManager(std::vector<AiFilterPtr> filters, JsonWithExtBuf payload_index,
                             BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
                             StreamInfo::StreamInfo& stream_info, LocalReplyFn local_reply_fn)
    : filters_(std::move(filters)), payload_index_(std::move(payload_index)),
      executor_(std::make_shared<Coroutine::DispatcherExecutor>(dispatcher)),
      async_state_(std::make_shared<AsyncState>(filters_.size(), buffer_manager, stream_info,
                                                executor_, std::move(local_reply_fn))) {}

FilterManager::~FilterManager() { cancel(); }

void FilterManager::start(absl::AnyInvocable<void(absl::Status)> on_complete) {
  async_state_->on_complete_ = std::move(on_complete);
  launchFilters();
  if (async_state_->terminated_) {
    return;
  }
  launchSink();
  if (async_state_->terminated_) {
    return;
  }
  async_state_->filter_contexts_[0].handoff->tryPush(
      std::make_unique<AiRequest>(std::move(payload_index_)));
}

void FilterManager::launchFilters() {
  for (size_t i = 0; i < filters_.size(); ++i) {
    if (async_state_->terminated_) {
      break;
    }
    std::weak_ptr<AsyncState> weak_state = async_state_;

    AiRequestReceiver receiver([weak_state, i]() -> Coroutine::Task<absl::StatusOr<AiRequestPtr>> {
      auto state = weak_state.lock();
      if (!state || state->terminated_) {
        co_return absl::CancelledError("filter manager cancelled or destroyed");
      }
      co_return co_await state->receiveRequest(i);
    });

    AiRequestPropagator propagator(
        [weak_state, i](AiRequestPtr req) -> Coroutine::Task<absl::Status> {
          auto state = weak_state.lock();
          if (!state || state->terminated_) {
            co_return absl::CancelledError("filter manager cancelled or destroyed");
          }
          co_return co_await state->propagateRequest(i, std::move(req));
        });

    LocalReplier replier = [weak_state](Http::Code code, std::string details) {
      if (auto state = weak_state.lock()) {
        state->triggerLocalReply(code, std::move(details));
      }
    };

    auto task = filters_[i]->decode(std::move(receiver), std::move(propagator), std::move(replier));
    auto handle = Coroutine::launch(
        std::move(task), executor_,
        [weak_state, i](absl::Status status) {
          if (auto state = weak_state.lock()) {
            state->onFilterCompletion(i, std::move(status));
          }
        },
        Coroutine::StartMode::Inline);
    if (!async_state_->terminated_) {
      async_state_->handles_.push_back(std::move(handle));
    }
  }
}

void FilterManager::launchSink() {
  std::weak_ptr<AsyncState> weak_state = async_state_;
  auto task = async_state_->runSink();
  auto handle = Coroutine::launch(
      std::move(task), executor_,
      [weak_state](absl::Status status) {
        if (!status.ok()) {
          if (auto state = weak_state.lock()) {
            state->onFilterError(std::move(status));
          }
        }
      },
      Coroutine::StartMode::Inline);
  if (!async_state_->terminated_) {
    async_state_->handles_.push_back(std::move(handle));
  }
}

void FilterManager::cancel() { async_state_->cancel(); }

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
