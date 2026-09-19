#include "source/extensions/filters/http/ai_protocol_manager/response_filter_manager.h"

#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/coroutine/async_queue.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_pipeline.h"
#include "source/extensions/filters/http/ai_protocol_manager/task_group.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Base of the async state, so the manager can own one without knowing which item type flows
// through it.
class ResponseFilterManager::AsyncState : public TaskGroup {
public:
  using TaskGroup::TaskGroup;
  virtual void start() = 0;
  virtual void onData(Buffer::Instance& data, bool end_stream) = 0;
  virtual void cancel() = 0;
};

namespace {

// Async state machinery, independent of what flows through it. `Item` is one unit of work passed
// between filters -- an SSE frame here; the unary mode adds a second instantiation carrying
// batches of response fields, which is why the item type is a parameter rather than fixed.
//
// Stages are numbered 0..N: stage i is the input queue of filter i, and stage N is the sink's.
// Each queue holds one item, so a filter that has not taken its current item blocks the stage
// behind it, all the way back to the decoder.
template <typename Item>
class ResponseAsyncState : public ResponseFilterManager::AsyncState,
                           public std::enable_shared_from_this<ResponseAsyncState<Item>>,
                           public Logger::Loggable<Logger::Id::ai_protocol_manager> {
public:
  ResponseAsyncState(std::vector<AiFilterSharedPtr> filters, FilterChainBridge& bridge,
                     BufferManager& out_buffer_manager,
                     ResponseFilterManager::OnCompleteFn on_complete)
      : ResponseFilterManager::AsyncState(bridge.dispatcher()), filters_(std::move(filters)),
        pipeline_(filters_.size()), bridge_(bridge), out_buffer_manager_(out_buffer_manager),
        on_complete_(std::move(on_complete)) {}

  ~ResponseAsyncState() override { cancel(); }

  void start() override {
    auto self = shared_from_this();
    std::weak_ptr<ResponseAsyncState> weak = weak_from_this();
    // Consumers first: every stage must be parked on its pop() before items are pushed, or the
    // first item would have nowhere to go and would buffer needlessly.
    for (size_t i = 0; i < filters_.size(); ++i) {
      launchTask(runFilter(i), [weak, i, filter = filters_[i]](absl::Status status) {
        if (auto s = weak.lock()) {
          s->onFilterCompletion(i, std::move(status));
        }
      });
    }
    launchTask(runSink(), [weak](absl::Status status) {
      if (auto s = weak.lock()) {
        if (!status.ok()) {
          s->fail(std::move(status));
        }
      }
    });
  }

  void onData(Buffer::Instance& data, bool end_stream) override {
    auto self = shared_from_this();
    if (terminated()) {
      data.drain(data.length());
      return;
    }

    std::vector<Item> decoded;
    if (data.length() > 0) {
      absl::Status status = decode(data, decoded);
      data.drain(data.length());
      if (!status.ok()) {
        fail(std::move(status));
        return;
      }
    }

    if (end_stream) {
      input_ended_ = true;
      absl::Status status = finishDecode(decoded);
      if (!status.ok()) {
        fail(std::move(status));
        return;
      }
    }

    for (Item& item : decoded) {
      if (!draining_pending_items_ && pending_items_.empty() &&
          pipeline_.stage(0)->tryPush(std::move(item))) {
        continue;
      }
      const uint64_t size = itemSize(item);
      pending_items_.push_back(PendingItem{std::move(item), {bridge_, size}});
    }

    if (!pending_items_.empty() && !draining_pending_items_) {
      draining_pending_items_ = true;
      std::weak_ptr<ResponseAsyncState> weak = weak_from_this();
      launchTask(drainPendingItems(), [weak](absl::Status status) {
        if (auto s = weak.lock()) {
          if (!status.ok()) {
            s->fail(std::move(status));
          }
        }
      });
      return;
    }

    if (input_ended_ && !draining_pending_items_ && !terminated()) {
      pipeline_.stage(0)->close();
    }
  }

  void cancel() override {
    if (terminated()) {
      return;
    }
    auto self = shared_from_this();
    on_complete_ = nullptr;
    cancelHandles();
    cleanupInput();
    pipeline_.closeAndDrain();
  }

  Coroutine::Task<absl::StatusOr<std::optional<Item>>> receive(size_t index) {
    return pipeline_.receive(index);
  }

  Coroutine::Task<absl::Status> propagate(size_t index, Item item) {
    return pipeline_.propagate(index, std::move(item));
  }

protected:
  using std::enable_shared_from_this<ResponseAsyncState<Item>>::shared_from_this;
  using std::enable_shared_from_this<ResponseAsyncState<Item>>::weak_from_this;

  void cleanupInput() {
    draining_pending_items_ = false;
    pending_items_.clear();
  }

  // Mode-specific hooks.

  // Returns the estimated byte size of `item` for watermark accounting when buffered in
  // pending_items_.
  virtual uint64_t itemSize(const Item& item) const = 0;
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
  FilterPipeline<Item> pipeline_;
  FilterChainBridge& bridge_;
  BufferManager& out_buffer_manager_;

private:
  struct PendingItem {
    Item item;
    FilterChainBridge::ScopedUnacked unacked;
  };

  // Drains overflow items that could not be pushed synchronously by onData(), releasing the bridge
  // as each item enters stage(0).
  Coroutine::Task<absl::Status> drainPendingItems() {
    auto self = shared_from_this();
    while (!pending_items_.empty()) {
      if (terminated()) {
        co_return absl::CancelledError("response pipeline cancelled");
      }
      PendingItem pending = std::move(pending_items_.front());
      pending_items_.pop_front();
      CO_RETURN_IF_ERROR(co_await pipeline_.stage(0)->push(std::move(pending.item)));
    }
    draining_pending_items_ = false;
    if (input_ended_ && !terminated()) {
      pipeline_.stage(0)->close();
    }
    co_return absl::OkStatus();
  }

  // Forwards a stage's input untouched, standing in for a filter that has bowed out.
  Coroutine::Task<absl::Status> bypassStage(size_t index) {
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item, co_await pipeline_.receive(index));
      if (!item.has_value()) {
        pipeline_.stage(index + 1)->close();
        co_return absl::OkStatus();
      }
      CO_RETURN_IF_ERROR(co_await pipeline_.propagate(index, std::move(*item)));
    }
  }

  Coroutine::Task<absl::Status> runSink() {
    const size_t index = pipeline_.numStages() - 1;
    while (true) {
      ASSIGN_OR_CO_RETURN(auto item, co_await pipeline_.receive(index));
      if (!item.has_value()) {
        break;
      }
      CO_RETURN_IF_ERROR(co_await serializeItem(std::move(*item)));
    }
    CO_RETURN_IF_ERROR(co_await finishSerialize());
    // Yield to the dispatcher before firing on_complete_ so completion never runs reentrantly
    // inside a filter's encodeData/encodeTrailers callback.
    CO_RETURN_IF_ERROR(co_await Coroutine::yield());
    auto self = shared_from_this();
    markTerminated();
    if (ResponseFilterManager::OnCompleteFn callback = std::move(on_complete_)) {
      callback(absl::OkStatus());
    }
    co_return absl::OkStatus();
  }

  void onFilterCompletion(size_t index, absl::Status status) {
    if (terminated()) {
      return;
    }
    if (!status.ok()) {
      fail(std::move(status));
      return;
    }
    if (pipeline_.stage(index)->closed() && pipeline_.stage(index)->empty()) {
      pipeline_.stage(index + 1)->close();
      return;
    }
    // The filter is done: splice it out and let the rest of the stream flow past it. An item it
    // received but never propagated stops here, which is a filter's own call to make -- merging
    // several frames into one, or dropping what it has buffered, is legitimate.
    std::weak_ptr<ResponseAsyncState> weak = weak_from_this();
    launchTask(bypassStage(index), [weak](absl::Status status) {
      if (auto s = weak.lock()) {
        if (!status.ok()) {
          s->fail(std::move(status));
        }
      }
    });
  }

  void fail(absl::Status status) {
    if (terminated()) {
      return;
    }
    auto self = shared_from_this();
    ENVOY_LOG(debug, "ai_protocol_manager: response filter chain error: {}", status.message());
    ResponseFilterManager::OnCompleteFn callback = std::move(on_complete_);
    cancel();
    if (callback != nullptr) {
      callback(std::move(status));
    }
  }

  ResponseFilterManager::OnCompleteFn on_complete_;
  std::deque<PendingItem> pending_items_;
  bool draining_pending_items_{false};
  bool input_ended_{false};
};

// SSE response pipeline implementation (`Item = SseEventPtr`).
//
// The SSE response arrives as raw bytes and leaves as raw bytes, while filters operate on parsed
// `SseEvent` frames via `AiFilter::encodeSSE()`. Between upstream and downstream sits
// `SseEventDecoder`, the filter coroutine chain, and `SseEventSerializer` in the sink.
class SseAsyncState : public ResponseAsyncState<SseEventPtr> {
public:
  SseAsyncState(std::vector<AiFilterSharedPtr> filters, ExternalBufferFactory& buffer_factory,
                FilterChainBridge& bridge, BufferManager& out_buffer_manager,
                ResponseFilterManager::OnCompleteFn on_complete,
                SseEventDecoder::Config decoder_config)
      : ResponseAsyncState(std::move(filters), bridge, out_buffer_manager, std::move(on_complete)),
        decoder_(decoder_config, buffer_factory, bridge) {}

  ~SseAsyncState() override { cancel(); }

protected:
  uint64_t itemSize(const SseEventPtr& item) const override {
    return item != nullptr ? item->byteSize() : 0;
  }

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
  receiveSseTask(std::weak_ptr<ResponseAsyncState<SseEventPtr>> weak, size_t index) {
    auto self = weak.lock();
    if (self == nullptr || self->terminated()) {
      co_return absl::CancelledError("response pipeline cancelled or destroyed");
    }
    co_return co_await self->receive(index);
  }

  static Coroutine::Task<absl::Status>
  propagateSseTask(std::weak_ptr<ResponseAsyncState<SseEventPtr>> weak, size_t index,
                   SseEventPtr event) {
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
    std::weak_ptr<ResponseAsyncState<SseEventPtr>> weak = weak_from_this();
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
  async_state_ =
      std::make_shared<SseAsyncState>(std::move(filters), buffer_factory, bridge,
                                      out_buffer_manager, std::move(on_complete), config.sse);
  // Started separately from construction: the stages capture weak references to the state, which
  // only exist once the shared_ptr does.
  async_state_->start();
}

ResponseFilterManager::~ResponseFilterManager() { cancel(); }

void ResponseFilterManager::onData(Buffer::Instance& data, bool end_stream) {
  auto state = async_state_;
  state->onData(data, end_stream);
}

void ResponseFilterManager::cancel() {
  auto state = async_state_;
  if (state != nullptr) {
    state->cancel();
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
