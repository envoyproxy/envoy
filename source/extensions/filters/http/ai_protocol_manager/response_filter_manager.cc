#include "source/extensions/filters/http/ai_protocol_manager/response_filter_manager.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"

#include "absl/status/statusor.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Base of the pipeline, so the manager can own one without knowing which item type flows through
// it.
class ResponseFilterManager::State {
public:
  virtual ~State() = default;
  virtual void start() = 0;
  virtual void onData(Buffer::Instance& data, bool end_stream) = 0;
  virtual void cancel() = 0;
};

namespace {

// Wakes the source coroutine. Carries nothing: the payload is whatever has accumulated in the
// pending input buffer by the time the source looks.
using Signal = absl::monostate;

// Chain machinery, independent of what flows through it. `Item` is one unit of work passed
// between filters -- an SSE frame here; the unary mode adds a second instantiation carrying
// batches of response fields, which is why the item type is a parameter rather than fixed.
//
// Stages are numbered 0..N: stage i is the input queue of filter i, and stage N is the sink's.
// Each queue holds one item, so a filter that has not taken its current item blocks the stage
// behind it, all the way back to the decoder.
template <typename Item>
class ChainState : public ResponseFilterManager::State,
                   public FilterPipeline<Item>,
                   public std::enable_shared_from_this<ChainState<Item>>,
                   public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  ChainState(std::vector<AiFilterSharedPtr> filters, FilterChainBridge& bridge,
             BufferManager& out_buffer_manager, ResponseFilterManager::OnCompleteFn on_complete)
      : FilterPipeline<Item>(filters.size(), bridge.dispatcher(), std::move(on_complete)),
        filters_(std::move(filters)), bridge_(bridge), out_buffer_manager_(out_buffer_manager),
        signal_(std::make_shared<Coroutine::AsyncQueue<Signal>>(/*max_size=*/1)) {}

  ~ChainState() override { cancel(); }

  void start() override {
    std::weak_ptr<ChainState> weak = this->weak_from_this();
    // Consumers first: every stage must be parked on its pop() before the source pushes, or the
    // first item would have nowhere to go and the source would block needlessly.
    for (size_t i = 0; i < filters_.size(); ++i) {
      this->launchTask(runFilter(i), [weak, i, filter = filters_[i]](absl::Status status) {
        if (auto self = weak.lock()) {
          self->onFilterCompletion(i, std::move(status));
        }
      });
    }
    this->launchTask(runSink(), [weak](absl::Status status) {
      if (auto self = weak.lock()) {
        if (!status.ok()) {
          self->fail(std::move(status));
        }
      }
    });
    this->launchTask(runSource(), [weak](absl::Status status) {
      if (auto self = weak.lock()) {
        if (!status.ok()) {
          self->fail(std::move(status));
        }
      }
    });
  }

  void onData(Buffer::Instance& data, bool end_stream) override {
    if (this->terminated()) {
      return;
    }
    const uint64_t len = data.length();
    pending_input_.move(data);
    if (len > 0) {
      bridge_.addUnacked(len);
    }
    if (end_stream) {
      input_ended_ = true;
    }
    // One slot: a signal already queued means the source has not caught up yet, and a second
    // would tell it nothing new.
    signal_->tryPush(Signal{});
  }

  void cancel() override { FilterPipeline<Item>::cancel(); }

protected:
  void onCancel() override {
    const uint64_t pending_len = pending_input_.length();
    pending_input_.drain(pending_len);
    if (pending_len > 0) {
      bridge_.releaseUnacked(pending_len);
    }
    signal_->close();
  }

  // Mode-specific hooks.

  // Decodes `data`, appending completed items to `out`.
  virtual absl::Status decode(const Buffer::Instance& data, std::vector<Item>& out) = 0;
  // Finishes decoding at end of stream, appending any trailing item to `out`.
  virtual absl::Status finishDecode(std::vector<Item>& out) = 0;
  // Writes one item back out through the buffer manager.
  virtual Coroutine::Task<absl::Status> serializeItem(Item item) = 0;
  // Writes anything the encoding needs after the last item.
  virtual Coroutine::Task<absl::Status> finishSerialize() = 0;
  // Runs filter `index`, handing it a receiver and propagator bound to its stage.
  virtual Coroutine::Task<absl::Status> runFilter(size_t index) = 0;

  std::vector<AiFilterSharedPtr> filters_;
  FilterChainBridge& bridge_;
  BufferManager& out_buffer_manager_;

private:
  // Decodes input and feeds the head of the chain. The only place that awaits on the input side,
  // which is what lets onData() stay non-blocking.
  Coroutine::Task<absl::Status> runSource() {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto signal, co_await signal_->pop());
      if (!signal.has_value()) {
        // Cancelled.
        co_return absl::OkStatus();
      }

      // More input may arrive while a push below is blocked, so drain until actually empty
      // rather than once per signal.
      while (pending_input_.length() > 0) {
        Buffer::OwnedImpl chunk;
        const uint64_t chunk_len = pending_input_.length();
        chunk.move(pending_input_);

        std::vector<Item> items;
        const absl::Status decode_status = decode(chunk, items);
        bridge_.releaseUnacked(chunk_len);
        CO_RETURN_IF_ERROR(decode_status);
        CO_RETURN_IF_ERROR(co_await pushAll(items));
      }

      if (input_ended_) {
        std::vector<Item> items;
        CO_RETURN_IF_ERROR(finishDecode(items));
        CO_RETURN_IF_ERROR(co_await pushAll(items));
        // Closing cascades: each stage's pop returns nullopt, the stage finishes, and the bypass
        // launched in its place closes the stage after it.
        this->stage(0)->close();
        co_return absl::OkStatus();
      }
    }
  }

  Coroutine::Task<absl::Status> pushAll(std::vector<Item>& items) {
    for (Item& item : items) {
      CO_RETURN_IF_ERROR(co_await this->stage(0)->push(std::move(item)));
    }
    co_return absl::OkStatus();
  }

  // Forwards a stage's input untouched, standing in for a filter that has bowed out.
  Coroutine::Task<absl::Status> bypassStage(size_t index) {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item, co_await this->receive(index));
      if (!item.has_value()) {
        this->stage(index + 1)->close();
        co_return absl::OkStatus();
      }
      CO_RETURN_IF_ERROR(co_await this->propagate(index, std::move(*item)));
    }
  }

  Coroutine::Task<absl::Status> runSink() {
    const size_t index = this->numStages() - 1;
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item, co_await this->receive(index));
      if (!item.has_value()) {
        break;
      }
      CO_RETURN_IF_ERROR(co_await serializeItem(std::move(*item)));
    }
    CO_RETURN_IF_ERROR(co_await finishSerialize());
    this->complete(absl::OkStatus());
    co_return absl::OkStatus();
  }

  void onFilterCompletion(size_t index, absl::Status status) {
    if (this->terminated()) {
      return;
    }
    if (!status.ok()) {
      fail(std::move(status));
      return;
    }
    // The filter is done: splice it out and let the rest of the stream flow past it. An item it
    // received but never propagated stops here, which is a filter's own call to make -- merging
    // several frames into one, or dropping what it has buffered, is legitimate.
    std::weak_ptr<ChainState> weak = this->weak_from_this();
    this->launchTask(bypassStage(index), [weak](absl::Status status) {
      if (auto self = weak.lock()) {
        if (!status.ok()) {
          self->fail(std::move(status));
        }
      }
    });
  }

  void fail(absl::Status status) {
    if (this->terminated()) {
      return;
    }
    ENVOY_LOG(debug, "ai_protocol_manager: response filter chain error: {}", status.message());
    auto callback = this->takeOnComplete();
    cancel();
    if (callback != nullptr) {
      callback(std::move(status));
    }
  }

  std::shared_ptr<Coroutine::AsyncQueue<Signal>> signal_;
  Buffer::OwnedImpl pending_input_;
  bool input_ended_{false};
};

class SseChainState : public ChainState<SseEventPtr> {
public:
  SseChainState(std::vector<AiFilterSharedPtr> filters, ExternalBufferFactory& buffer_factory,
                FilterChainBridge& bridge, BufferManager& out_buffer_manager,
                ResponseFilterManager::OnCompleteFn on_complete,
                SseEventDecoder::Config decoder_config)
      : ChainState(std::move(filters), bridge, out_buffer_manager, std::move(on_complete)),
        decoder_(decoder_config, buffer_factory, bridge) {}

  ~SseChainState() override { cancel(); }

protected:
  absl::Status decode(const Buffer::Instance& data, std::vector<SseEventPtr>& out) override {
    return decoder_.onData(data, out);
  }

  absl::Status finishDecode(std::vector<SseEventPtr>& out) override {
    return decoder_.onEndStream(out);
  }

  Coroutine::Task<absl::Status> serializeItem(SseEventPtr item) override {
    co_return co_await SseEventSerializer::serialize(*item, out_buffer_manager_);
  }

  Coroutine::Task<absl::Status> finishSerialize() override { co_return absl::OkStatus(); }

  static Coroutine::Task<absl::StatusOr<std::optional<SseEventPtr>>>
  receiveSseTask(std::weak_ptr<ChainState<SseEventPtr>> weak, size_t index) {
    auto self = weak.lock();
    if (self == nullptr || self->terminated()) {
      co_return absl::CancelledError("response pipeline cancelled or destroyed");
    }
    co_return co_await self->receive(index);
  }

  static Coroutine::Task<absl::Status> propagateSseTask(std::weak_ptr<ChainState<SseEventPtr>> weak,
                                                        size_t index, SseEventPtr event) {
    auto self = weak.lock();
    if (self == nullptr || self->terminated()) {
      co_return absl::CancelledError("response pipeline cancelled or destroyed");
    }
    if (event == nullptr) {
      // Checked here rather than trusted: the serializer would dereference it, so a filter
      // with this bug would take the process down instead of the response.
      IS_ENVOY_BUG("cannot propagate a null SseEventPtr");
      co_return absl::InvalidArgumentError("cannot propagate a null SseEventPtr");
    }
    co_return co_await self->propagate(index, std::move(event));
  }

  Coroutine::Task<absl::Status> runFilter(size_t index) override {
    // Weak, not strong: the filter's coroutine frame is owned by a handle this object holds, so a
    // strong reference here would be a cycle.
    std::weak_ptr<ChainState<SseEventPtr>> weak = this->weak_from_this();
    SseStreamReceiver receiver([weak, index]() { return receiveSseTask(weak, index); });
    SseStreamPropagator propagator([weak, index](SseEventPtr event) {
      return propagateSseTask(weak, index, std::move(event));
    });
    co_return co_await filters_[index]->encodeSSE(std::move(receiver), std::move(propagator));
  }

private:
  SseEventDecoder decoder_;
};

} // namespace

ResponseFilterManager::ResponseFilterManager(std::vector<AiFilterSharedPtr> filters,
                                             ExternalBufferFactory& buffer_factory,
                                             FilterChainBridge& bridge,
                                             BufferManager& out_buffer_manager,
                                             OnCompleteFn on_complete, Config config) {
  state_ = std::make_shared<SseChainState>(std::move(filters), buffer_factory, bridge,
                                           out_buffer_manager, std::move(on_complete), config.sse);
  // Started separately from construction: the stages capture weak references to the state, which
  // only exist once the shared_ptr does.
  state_->start();
}

ResponseFilterManager::~ResponseFilterManager() {
  if (state_ != nullptr) {
    state_->cancel();
  }
}

void ResponseFilterManager::onData(Buffer::Instance& data, bool end_stream) {
  state_->onData(data, end_stream);
}

void ResponseFilterManager::cancel() { state_->cancel(); }

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
