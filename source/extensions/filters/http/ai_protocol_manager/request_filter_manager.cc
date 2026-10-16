#include "source/extensions/filters/http/ai_protocol_manager/request_filter_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"
#include "source/extensions/filters/http/ai_protocol_manager/replay_awaitable.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"
#include "source/extensions/filters/http/ai_protocol_manager/task_group.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

constexpr absl::string_view FilterRejectedDetails = "ai_protocol_manager_filter_rejected";

} // namespace

// One request stage's AiFilterCallbacks. A filter can outlive the chain, so this holds the chain
// weakly and owns its name.
class StageCallbacks : public AiFilterCallbacks,
                       public UpstreamCandidatesEditor,
                       public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  StageCallbacks(std::weak_ptr<RequestFilterManager::AsyncState> state, size_t index,
                 AiFilterStage stage, bool editor_enabled)
      : state_(std::move(state)), index_(index), stage_(std::move(stage)),
        editor_enabled_(editor_enabled) {}

  // AiFilterCallbacks
  absl::string_view filterName() const override { return stage_.name; }
  OptRef<UpstreamCandidatesEditor> upstreamCandidates() override {
    if (!editor_enabled_) {
      return {};
    }
    return makeOptRef<UpstreamCandidatesEditor>(*this);
  }

  // UpstreamCandidatesEditor
  const UpstreamCandidates* current() const override;
  absl::Status seed(std::vector<UpstreamCandidate> candidates, absl::string_view detail) override;

private:
  const std::weak_ptr<RequestFilterManager::AsyncState> state_;
  const size_t index_;
  const AiFilterStage stage_;
  const bool editor_enabled_;
};

class RequestFilterManager::AsyncState
    : public TaskGroup,
      public std::enable_shared_from_this<RequestFilterManager::AsyncState>,
      public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  AsyncState(std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
             BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
             StreamInfo::StreamInfo& stream_info, OnCompleteFn on_complete,
             Http::RequestHeaderMap* request_headers, LocalReplyFn local_reply_fn,
             bool always_serialize, LLMProtocol request_protocol, OnFiltersDoneFn on_filters_done,
             std::vector<AiFilterStage> stages, bool upstream_candidates_enabled)
      : TaskGroup(dispatcher), filters_(std::move(filters)),
        payload_index_(std::move(payload_index)), pipeline_(filters_.size()),
        buffer_manager_(buffer_manager), stream_info_(stream_info),
        on_complete_(std::move(on_complete)), request_headers_(request_headers),
        local_reply_fn_(std::move(local_reply_fn)), always_serialize_(always_serialize),
        request_protocol_(request_protocol), on_filters_done_(std::move(on_filters_done)),
        stages_(std::move(stages)), upstream_candidates_enabled_(upstream_candidates_enabled),
        filter_handoff_status_(filters_.size()) {
    ASSERT(stages_.empty() || stages_.size() == filters_.size());
  }

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
    bool payload_index_pushed = pipeline_.stage(0)->tryPush(
        std::make_unique<AiRequest>(std::move(payload_index_), request_protocol_));
    ASSERT(payload_index_pushed);
  }

  void cancel() {
    if (terminated()) {
      return;
    }
    auto self = shared_from_this();
    on_complete_ = nullptr;
    local_reply_fn_ = nullptr;
    on_filters_done_ = nullptr;
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
  friend class StageCallbacks;

  // Whether stage `index` may read or edit the decision: between its receive and its propagate.
  // A bypassed stage never receives, so it never opens one.
  bool inWindow(size_t index) const {
    return !terminated() && !sealed_ && filter_handoff_status_[index].received &&
           !filter_handoff_status_[index].propagated;
  }

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

      LocalReplier replier = [weak_state](Http::Code code, std::string body) {
        if (auto state = weak_state.lock()) {
          state->triggerLocalReply(code, std::move(body), FilterRejectedDetails);
        }
      };

      filters_[i]->setAiFilterCallbacks(std::make_shared<StageCallbacks>(
          weak_state, i, i < stages_.size() ? std::move(stages_[i]) : AiFilterStage{},
          upstream_candidates_enabled_));
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

    sealed_ = true;
    if (OnFiltersDoneFn on_filters_done = std::move(on_filters_done_)) {
      if (std::optional<SinkReply> reply = on_filters_done(*final_req_, decision_);
          reply.has_value()) {
        // Cancelling only flags this running coroutine, so returning OK here is safe.
        triggerLocalReply(reply->code, std::move(reply->body), reply->details);
        co_return absl::OkStatus();
      }
    }

    if (always_serialize_) {
      CO_RETURN_IF_ERROR(co_await serializeFinalRequest());
    } else {
      CO_RETURN_IF_ERROR(co_await forwardReceivedBody());
    }

    auto self = shared_from_this();
    markTerminated();
    if (OnCompleteFn on_complete = std::move(on_complete_)) {
      on_complete(absl::OkStatus());
    }
    co_return absl::OkStatus();
  }

  Coroutine::Task<absl::Status> serializeFinalRequest() {
    ASSIGN_OR_CO_RETURN(
        Serializer::SerializedOffsets serialized_offsets,
        co_await Serializer::calculateSerializedOffsets(final_req_->request_index()));
    publishPayloadIndex(std::move(serialized_offsets.doc));
    setContentLength(serialized_offsets.total_size);
    ASSIGN_OR_CO_RETURN(std::ignore,
                        co_await Serializer::serialize(final_req_->request_index(), buffer_manager_,
                                                       buffer_manager_));
    co_return absl::OkStatus();
  }

  // The request's own index already locates its external references in the received body.
  Coroutine::Task<absl::Status> forwardReceivedBody() {
    const uint64_t length = buffer_manager_->length();
    publishPayloadIndex(final_req_->takeRequestIndex());
    setContentLength(length);
    co_return co_await ReplayAwaitable(*buffer_manager_, 0, length);
  }

  void publishPayloadIndex(JsonWithExtBuf index) {
    if (stream_info_.filterState() != nullptr) {
      stream_info_.filterState()->setData(
          APMRequestPayloadIndex::kFilterStateKey,
          std::make_shared<APMRequestPayloadIndex>(std::move(index)),
          StreamInfo::FilterState::LifeSpan::Request);
    }
  }

  void setContentLength(uint64_t length) {
    if (request_headers_ != nullptr && request_headers_->ContentLength() != nullptr) {
      request_headers_->setContentLength(length);
    }
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
      reply_fn(Http::Code::BadGateway, std::string(status.message()), FilterRejectedDetails);
    }
    if (on_complete != nullptr) {
      on_complete(std::move(status));
    }
  }

  void triggerLocalReply(Http::Code code, std::string body, absl::string_view details) {
    if (terminated()) {
      return;
    }
    auto self = shared_from_this();
    LocalReplyFn reply_fn = std::move(local_reply_fn_);
    OnCompleteFn on_complete = std::move(on_complete_);
    cancel();
    ENVOY_LOG(debug, "ai_protocol_manager: filter chain triggered local reply: {} {} ({})",
              static_cast<uint32_t>(code), body, details);

    if (reply_fn != nullptr) {
      reply_fn(code, std::move(body), details);
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
  const bool always_serialize_{true};
  const LLMProtocol request_protocol_;
  OnFiltersDoneFn on_filters_done_;
  // Moved into each stage's callbacks at launch.
  std::vector<AiFilterStage> stages_;
  const bool upstream_candidates_enabled_;
  // Created by the first seed; held per stream, so replacing the AiRequest does not drop it.
  std::shared_ptr<UpstreamCandidates> decision_;
  // Set once the sink has the request; no edits after.
  bool sealed_{false};
  AiRequestPtr final_req_;
  std::vector<FilterHandoffStatus> filter_handoff_status_;
};

const UpstreamCandidates* StageCallbacks::current() const {
  std::shared_ptr<RequestFilterManager::AsyncState> state = state_.lock();
  return state != nullptr && state->inWindow(index_) ? state->decision_.get() : nullptr;
}

absl::Status StageCallbacks::seed(std::vector<UpstreamCandidate> candidates,
                                  absl::string_view detail) {
  std::shared_ptr<RequestFilterManager::AsyncState> state = state_.lock();
  absl::Status status;
  if (state == nullptr || !state->inWindow(index_)) {
    status = absl::FailedPreconditionError(
        "upstream candidates are editable only between receiving and propagating the request");
  } else if (!stage_.policy.seed) {
    status = absl::PermissionDeniedError("this AI filter may not seed upstream candidates");
  } else if (state->decision_ != nullptr) {
    status = absl::AlreadyExistsError("an earlier AI filter seeded upstream candidates");
  } else {
    absl::StatusOr<std::shared_ptr<UpstreamCandidates>> decision = UpstreamCandidates::create(
        std::move(candidates),
        detail.empty() ? stage_.name : absl::StrCat(stage_.name, "/", detail));
    if (decision.ok()) {
      state->decision_ = std::move(*decision);
      ENVOY_LOG(debug, "ai_protocol_manager: AI filter '{}' seeded {} upstream candidates",
                stage_.name, state->decision_->entries().size());
      return absl::OkStatus();
    }
    status = decision.status();
  }
  ENVOY_LOG(debug, "ai_protocol_manager: refused upstream candidates seed from AI filter '{}': {}",
            stage_.name, status.message());
  return status;
}

RequestFilterManager::RequestFilterManager(
    std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
    BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
    StreamInfo::StreamInfo& stream_info, OnCompleteFn on_complete,
    Http::RequestHeaderMap* request_headers, LocalReplyFn local_reply_fn, bool always_serialize,
    LLMProtocol request_protocol, OnFiltersDoneFn on_filters_done,
    std::vector<AiFilterStage> stages, bool upstream_candidates_enabled)
    : async_state_(std::make_shared<AsyncState>(
          std::move(filters), std::move(payload_index), buffer_manager, dispatcher, stream_info,
          std::move(on_complete), request_headers, std::move(local_reply_fn), always_serialize,
          request_protocol, std::move(on_filters_done), std::move(stages),
          upstream_candidates_enabled)) {}

RequestFilterManager::~RequestFilterManager() { cancel(); }

void RequestFilterManager::start() {
  auto state = async_state_;
  state->start();
}

void RequestFilterManager::cancel() {
  auto state = async_state_;
  state->cancel();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
