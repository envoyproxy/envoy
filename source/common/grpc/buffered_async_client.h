#pragma once

#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Grpc {

enum class BufferState { Buffered, PendingFlush };

template <class RequestType, class ResponseType> class BufferedAsyncClient {
public:
  BufferedAsyncClient(uint32_t max_buffer_bytes, const Protobuf::MethodDescriptor& service_method,
                      Grpc::AsyncStreamCallbacks<ResponseType>& callbacks,
                      const Grpc::AsyncClient<RequestType, ResponseType>& client)
      : max_buffer_bytes_(max_buffer_bytes), service_method_(service_method), callbacks_(callbacks),
        client_(client) {}

  virtual ~BufferedAsyncClient() { cleanup(); }

  uint32_t publishId(RequestType& message) { return MessageUtil::hash(message); }

  void bufferMessage(uint32_t id, RequestType& message) {
    const auto buffer_size = message.ByteSizeLong();
    if (current_buffer_bytes_ + buffer_size > max_buffer_bytes_) {
      return;
    }

    message_buffer_[id] = std::make_pair(BufferState::Buffered, message);
    current_buffer_bytes_ += buffer_size;
  }

  absl::flat_hash_set<uint32_t> sendBufferedMessages() {
    if (active_stream_ == nullptr) {
      active_stream_ =
          client_.start(service_method_, callbacks_, Http::AsyncClient::StreamOptions());
    }

    if (active_stream_->isAboveWriteBufferHighWatermark()) {
      return {};
    }

    absl::flat_hash_set<uint32_t> inflight_message_ids;

    for (auto&& it : message_buffer_) {
      const auto id = it.first;
      auto& state = it.second.first;
      auto& message = it.second.second;

      if (state == BufferState::PendingFlush) {
        continue;
      }

      state = BufferState::PendingFlush;
      inflight_message_ids.emplace(id);
      active_stream_->sendMessage(message, false);
    }

    return inflight_message_ids;
  }

  void onSuccess(uint32_t message_id) { erasePendingMessage(message_id); }

  void onError(uint32_t message_id) {
    if (message_buffer_.find(message_id) == message_buffer_.end()) {
      return;
    }
    message_buffer_.at(message_id).first = BufferState::Buffered;
  }

  void cleanup() {
    if (active_stream_ != nullptr) {
      active_stream_ = nullptr;
    }
  }

  bool hasActiveStream() { return active_stream_ != nullptr; }

  const absl::flat_hash_map<uint32_t, std::pair<BufferState, RequestType>>& messageBuffer() {
    return message_buffer_;
  }

private:
  void erasePendingMessage(uint32_t message_id) {
    if (message_buffer_.find(message_id) == message_buffer_.end()) {
      return;
    }
    auto& buffer = message_buffer_.at(message_id);

    // There may be cases where the buffer status is not PendingFlush when
    // this function is called. For example, a message_buffer that was
    // PendingFlush may become Buffered due to an external state change
    // (e.g. re-buffering due to timeout).
    if (buffer.first == BufferState::PendingFlush) {
      const auto buffer_size = buffer.second.ByteSizeLong();
      current_buffer_bytes_ -= buffer_size;
      message_buffer_.erase(message_id);
    }
  }

  uint32_t max_buffer_bytes_ = 0;
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncStreamCallbacks<ResponseType>& callbacks_;
  Grpc::AsyncClient<RequestType, ResponseType> client_;
  Grpc::AsyncStream<RequestType> active_stream_;
  absl::flat_hash_map<uint32_t, std::pair<BufferState, RequestType>> message_buffer_;
  uint32_t current_buffer_bytes_ = 0;
};

template <class RequestType, class ResponseType>
using BufferedAsyncClientPtr = std::unique_ptr<BufferedAsyncClient<RequestType, ResponseType>>;

} // namespace Grpc
} // namespace Envoy
