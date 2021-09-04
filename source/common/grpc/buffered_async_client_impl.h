#pragma once

#include "source/common/grpc/typed_async_client.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Grpc {

enum class BufferState { Buffered, Pending };

template <class RequestType, class ResponseType> class BufferedAsyncClient {
public:
  BufferedAsyncClient(uint32_t max_buffer_bytes, const Protobuf::MethodDescriptor& service_method,
                      Grpc::AsyncStreamCallbacks<ResponseType>& callbacks,
                      const Grpc::AsyncClient<RequestType, ResponseType>& client)
      : max_buffer_bytes_(max_buffer_bytes), service_method_(service_method), callbacks_(callbacks),
        client_(client) {}

  ~BufferedAsyncClient() {
    if (active_stream_ != nullptr) {
      // active_stream_->resetStream();
    }
  }

  uint32_t publishId(RequestType& message) { return MessageUtil::hash(message); }

  void bufferMessage(uint32_t id, RequestType& message) {
    const auto buffer_size = message.ByteSizeLong();
    if (current_buffer_bytes_ + buffer_size > max_buffer_bytes_) {
      return;
    }

    message_buffer_[id] = std::make_pair(BufferState::Buffered, message);
    current_buffer_bytes_ += max_buffer_bytes_;
  }

  void bufferMessage(uint32_t id) {
    if (message_buffer_.find(id) == message_buffer_.end()) {
      return;
    }
    message_buffer_.at(id).first = BufferState::Buffered;
  }

  std::vector<uint32_t> sendBufferedMessages() {
    if (active_stream_ == nullptr) {
      active_stream_ =
          client_.start(service_method_, callbacks_, Http::AsyncClient::StreamOptions());
    }
    std::vector<uint32_t> inflight_message_ids;

    if (active_stream_->isAboveWriteBufferHighWatermark()) {
      resetStream();
      return inflight_message_ids;
    }
    for (auto&& it : message_buffer_) {
      const auto id = it.first;
      auto& state = it.second.first;
      auto& message = it.second.second;

      if (state == BufferState::Pending) {
        continue;
      }

      state = BufferState::Pending;
      inflight_message_ids.push_back(id);
      active_stream_->sendMessage(message, false);
    }

    return inflight_message_ids;
  }

  void clearPendingMessage(uint32_t id) {
    if (message_buffer_.find(id) == message_buffer_.end()) {
      return;
    }
    auto& buffer = message_buffer_.at(id);

    if (buffer.first == BufferState::Pending) {
      const auto buffer_size = buffer.second.ByteSizeLong();
      current_buffer_bytes_ -= buffer_size;
      message_buffer_.erase(id);
    }
  }

  void resetStream() {
    if (active_stream_ != nullptr) {
      active_stream_ = nullptr;
    }
  }

  bool hasActiveStream() { return active_stream_ != nullptr; }

private:
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
