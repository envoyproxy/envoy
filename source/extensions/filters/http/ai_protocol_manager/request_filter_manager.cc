#include "source/extensions/filters/http/ai_protocol_manager/request_filter_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

struct RequestFilterManager::AsyncState
    : public FilterPipeline<AiRequestPtr>,
      public std::enable_shared_from_this<RequestFilterManager::AsyncState>,
      public Logger::Loggable<Logger::Id::ai_protocol_manager> {
  struct FilterStatus {
    bool received{false};
    bool propagated{false};
  };

  AsyncState(size_t num_filters, BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
             StreamInfo::StreamInfo& stream_info, Http::RequestHeaderMap* request_headers,
             LocalReplyFn local_reply_fn)
      : FilterPipeline<AiRequestPtr>(num_filters, dispatcher), buffer_manager_(buffer_manager),
        stream_info_(stream_info), request_headers_(request_headers),
        local_reply_fn_(std::move(local_reply_fn)), filter_status_(num_filters) {}

  ~AsyncState() override { cancel(); }

  static Coroutine::Task<absl::StatusOr<AiRequestPtr>>
  receiveRequestTask(std::weak_ptr<AsyncState> weak_state, size_t index) {
    auto state = weak_state.lock();
    if (!state || state->terminated()) {
      co_return absl::CancelledError("filter manager cancelled or destroyed");
    }
    co_return co_await state->receiveRequest(index);
  }

  static Coroutine::Task<absl::Status> propagateRequestTask(std::weak_ptr<AsyncState> weak_state,
                                                            size_t index, AiRequestPtr req) {
    auto state = weak_state.lock();
    if (!state || state->terminated()) {
      co_return absl::CancelledError("filter manager cancelled or destroyed");
    }
    co_return co_await state->propagateRequest(index, std::move(req));
  }

  Coroutine::Task<absl::StatusOr<AiRequestPtr>> receiveRequest(size_t index) {
    ASSIGN_OR_CO_RETURN(auto res, co_await receive(index));
    if (!res.has_value()) {
      co_return absl::InternalError("request handoff queue closed unexpectedly");
    }
    filter_status_[index].received = true;
    co_return std::move(*res);
  }

  Coroutine::Task<absl::Status> bypassFilter(size_t index) {
    ASSIGN_OR_CO_RETURN(auto res, co_await receive(index));
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
    filter_status_[index].propagated = true;
    co_return co_await propagate(index, std::move(req));
  }

  Coroutine::Task<absl::Status> runSink() {
    ASSIGN_OR_CO_RETURN(auto res, co_await receive(numStages() - 1));
    if (!res.has_value()) {
      co_return absl::InternalError("sink handoff queue closed unexpectedly");
    }

    final_req_ = std::move(*res);
    ASSERT(final_req_ != nullptr);

    ASSIGN_OR_CO_RETURN(
        Serializer::SerializedOffsets serialized_offsets,
        co_await Serializer::calculateSerializedOffsets(final_req_->request_index()));

    if (stream_info_.filterState() != nullptr) {
      stream_info_.filterState()->setData(
          APMRequestPayloadIndex::kFilterStateKey,
          std::make_shared<APMRequestPayloadIndex>(std::move(serialized_offsets.doc)),
          StreamInfo::FilterState::LifeSpan::Request);
    }

    if (request_headers_ != nullptr && request_headers_->ContentLength() != nullptr) {
      request_headers_->setContentLength(serialized_offsets.total_size);
    }

    // TODO(penguingao): condition the serialization on config. If we want to
    // normalize the json payload / protocol or the payload is modified, we
    // re-serialize, if not, we just passthrough the original body.
    ASSIGN_OR_CO_RETURN(std::ignore,
                        co_await Serializer::serialize(final_req_->request_index(), buffer_manager_,
                                                       buffer_manager_));

    complete(absl::OkStatus());
    co_return absl::OkStatus();
  }

  void onFilterCompletion(size_t index, absl::Status status) {
    if (terminated()) {
      return;
    }
    if (!status.ok()) {
      onFilterError(std::move(status));
      return;
    }

    if (filter_status_[index].propagated) {
      return;
    }

    if (!filter_status_[index].received) {
      // Filter early-returned without calling receive_request (bypassed itself).
      // Forward the request in its handoff queue directly to the next stage.
      std::weak_ptr<AsyncState> weak_self = shared_from_this();
      launchTask(bypassFilter(index), [weak_self, index](absl::Status status) {
        if (auto self = weak_self.lock()) {
          self->onFilterCompletion(index, std::move(status));
        }
      });
      return;
    }

    // Filter called receive_request, but completed without calling propagateRequest or
    // reply_locally.
    onFilterError(absl::InternalError(
        "filter consumed request but terminated without propagating or replying locally"));
  }

  void onFilterError(absl::Status status) {
    if (terminated()) {
      return;
    }
    auto reply_fn = std::move(local_reply_fn_);
    local_reply_fn_ = nullptr;
    auto on_complete = takeOnComplete();
    cancel();
    ENVOY_LOG(debug, "ai_protocol_manager: filter chain error: {}", status.message());

    if (reply_fn != nullptr) {
      reply_fn(Http::Code::BadGateway, std::string(status.message()));
    }
    if (on_complete != nullptr) {
      on_complete(std::move(status));
    }
  }

  void triggerLocalReply(Http::Code code, std::string details) {
    if (terminated()) {
      return;
    }
    auto reply_fn = std::move(local_reply_fn_);
    local_reply_fn_ = nullptr;
    auto on_complete = takeOnComplete();
    cancel();
    ENVOY_LOG(debug, "ai_protocol_manager: filter chain triggered local reply: {} {}",
              static_cast<uint32_t>(code), details);

    if (reply_fn != nullptr) {
      reply_fn(code, std::move(details));
    }
    if (on_complete != nullptr) {
      on_complete(absl::CancelledError("local reply sent"));
    }
  }

  BufferManager* buffer_manager_{nullptr};
  StreamInfo::StreamInfo& stream_info_;
  Http::RequestHeaderMap* request_headers_{nullptr};
  LocalReplyFn local_reply_fn_;
  AiRequestPtr final_req_;
  std::vector<FilterStatus> filter_status_;
};

RequestFilterManager::RequestFilterManager(std::vector<AiFilterSharedPtr> filters,
                                           JsonWithExtBuf payload_index,
                                           BufferManager* buffer_manager,
                                           Event::Dispatcher& dispatcher,
                                           StreamInfo::StreamInfo& stream_info,
                                           Http::RequestHeaderMap* request_headers,
                                           LocalReplyFn local_reply_fn)
    : filters_(std::move(filters)), payload_index_(std::move(payload_index)),
      async_state_(std::make_shared<AsyncState>(filters_.size(), buffer_manager, dispatcher,
                                                stream_info, request_headers,
                                                std::move(local_reply_fn))) {}

RequestFilterManager::~RequestFilterManager() { cancel(); }

void RequestFilterManager::start(OnCompleteFn on_complete) {
  async_state_->setOnComplete(std::move(on_complete));
  launchFilters();
  if (async_state_->terminated()) {
    return;
  }
  launchSink();
  if (async_state_->terminated()) {
    return;
  }
  bool payload_index_pushed =
      async_state_->stage(0)->tryPush(std::make_unique<AiRequest>(std::move(payload_index_)));
  ASSERT(payload_index_pushed);
}

void RequestFilterManager::launchFilters() {
  for (size_t i = 0; i < filters_.size(); ++i) {
    if (async_state_->terminated()) {
      break;
    }
    std::weak_ptr<AsyncState> weak_state = async_state_;

    AiRequestReceiver receiver(
        [weak_state, i]() { return AsyncState::receiveRequestTask(weak_state, i); });

    AiRequestPropagator propagator([weak_state, i](AiRequestPtr req) {
      return AsyncState::propagateRequestTask(weak_state, i, std::move(req));
    });

    LocalReplier replier = [weak_state](Http::Code code, std::string details) {
      if (auto state = weak_state.lock()) {
        state->triggerLocalReply(code, std::move(details));
      }
    };

    auto task = filters_[i]->decode(std::move(receiver), std::move(propagator), std::move(replier));
    // Holds the filter until its coroutine completes, which can be after ~RequestFilterManager.
    async_state_->launchTask(std::move(task),
                             [weak_state, i, filter = filters_[i]](absl::Status status) {
                               if (auto state = weak_state.lock()) {
                                 state->onFilterCompletion(i, std::move(status));
                               }
                             });
  }
}

void RequestFilterManager::launchSink() {
  std::weak_ptr<AsyncState> weak_state = async_state_;
  async_state_->launchTask(async_state_->runSink(), [weak_state](absl::Status status) {
    if (!status.ok()) {
      if (auto state = weak_state.lock()) {
        state->onFilterError(std::move(status));
      }
    }
  });
}

void RequestFilterManager::cancel() { async_state_->cancel(); }

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
