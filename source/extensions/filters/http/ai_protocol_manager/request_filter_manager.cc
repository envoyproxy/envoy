#include "source/extensions/filters/http/ai_protocol_manager/request_filter_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"
#include "source/extensions/filters/http/ai_protocol_manager/task_group.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class RequestFilterManager::AsyncState
    : public TaskGroup,
      public std::enable_shared_from_this<RequestFilterManager::AsyncState>,
      public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  AsyncState(std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
             BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
             StreamInfo::StreamInfo& stream_info, OnCompleteFn on_complete,
             Http::RequestHeaderMap* request_headers, LocalReplyFn local_reply_fn)
      : TaskGroup(dispatcher), filters_(std::move(filters)),
        payload_index_(std::move(payload_index)), pipeline_(filters_.size()),
        buffer_manager_(buffer_manager), stream_info_(stream_info),
        on_complete_(std::move(on_complete)), request_headers_(request_headers),
        local_reply_fn_(std::move(local_reply_fn)), filter_handoff_status_(filters_.size()) {}

  ~AsyncState() override { cancel(); }

  void start() {
    auto self = shared_from_this();
    launchFilters();
    if (terminated()) {
      return;
    }
    launchSink();
    if (terminated()) {
      return;
    }
    bool payload_index_pushed =
        pipeline_.stage(0)->tryPush(std::make_unique<AiRequest>(std::move(payload_index_)));
    ASSERT(payload_index_pushed);
  }

  void cancel() {
    if (terminated()) {
      return;
    }
    auto self = shared_from_this();
    on_complete_ = nullptr;
    local_reply_fn_ = nullptr;
    cancelHandles();
    final_req_.reset();
    pipeline_.closeAndDrain();
  }

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

private:
  void launchFilters() {
    for (size_t i = 0; i < filters_.size(); ++i) {
      if (terminated()) {
        break;
      }
      std::weak_ptr<AsyncState> weak_state = shared_from_this();

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

      auto task =
          filters_[i]->decode(std::move(receiver), std::move(propagator), std::move(replier));
      // Holds the filter until its coroutine completes, which can be after ~RequestFilterManager.
      launchTask(std::move(task), [weak_state, i, filter = filters_[i]](absl::Status status) {
        if (auto state = weak_state.lock()) {
          state->onFilterCompletion(i, std::move(status));
        }
      });
    }
  }

  void launchSink() {
    std::weak_ptr<AsyncState> weak_state = shared_from_this();
    launchTask(runSink(), [weak_state](absl::Status status) {
      if (auto state = weak_state.lock()) {
        if (!status.ok()) {
          state->onFilterError(std::move(status));
        }
      }
    });
  }

  Coroutine::Task<absl::StatusOr<AiRequestPtr>> receiveRequest(size_t index) {
    ASSIGN_OR_CO_RETURN(auto res, co_await pipeline_.receive(index));
    if (!res.has_value()) {
      co_return absl::InternalError("request handoff queue closed unexpectedly");
    }
    filter_handoff_status_[index].received = true;
    co_return std::move(*res);
  }

  Coroutine::Task<absl::Status> bypassFilter(size_t index) {
    ASSIGN_OR_CO_RETURN(auto res, co_await pipeline_.receive(index));
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
    filter_handoff_status_[index].propagated = true;
    co_return co_await pipeline_.propagate(index, std::move(req));
  }

  Coroutine::Task<absl::Status> runSink() {
    ASSIGN_OR_CO_RETURN(auto res, co_await pipeline_.receive(pipeline_.numStages() - 1));
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

    auto self = shared_from_this();
    markTerminated();
    if (OnCompleteFn on_complete = std::move(on_complete_)) {
      on_complete(absl::OkStatus());
    }
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

    if (filter_handoff_status_[index].propagated) {
      return;
    }

    if (!filter_handoff_status_[index].received) {
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
    auto self = shared_from_this();
    LocalReplyFn reply_fn = std::move(local_reply_fn_);
    OnCompleteFn on_complete = std::move(on_complete_);
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
    auto self = shared_from_this();
    LocalReplyFn reply_fn = std::move(local_reply_fn_);
    OnCompleteFn on_complete = std::move(on_complete_);
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

  struct FilterHandoffStatus {
    bool received{false};
    bool propagated{false};
  };

  std::vector<AiFilterSharedPtr> filters_;
  JsonWithExtBuf payload_index_;
  FilterPipeline<AiRequestPtr> pipeline_;
  BufferManager* buffer_manager_{nullptr};
  StreamInfo::StreamInfo& stream_info_;
  OnCompleteFn on_complete_;
  Http::RequestHeaderMap* request_headers_{nullptr};
  LocalReplyFn local_reply_fn_;
  AiRequestPtr final_req_;
  std::vector<FilterHandoffStatus> filter_handoff_status_;
};

RequestFilterManager::RequestFilterManager(
    std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
    BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
    StreamInfo::StreamInfo& stream_info, OnCompleteFn on_complete,
    Http::RequestHeaderMap* request_headers, LocalReplyFn local_reply_fn)
    : async_state_(std::make_shared<AsyncState>(
          std::move(filters), std::move(payload_index), buffer_manager, dispatcher, stream_info,
          std::move(on_complete), request_headers, std::move(local_reply_fn))) {
  async_state_->start();
}

RequestFilterManager::~RequestFilterManager() { cancel(); }

void RequestFilterManager::cancel() {
  auto state = async_state_;
  state->cancel();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
