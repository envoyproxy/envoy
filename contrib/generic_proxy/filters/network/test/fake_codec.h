#pragma once

#include <cstdint>

#include "source/common/buffer/buffer_impl.h"

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

template <typename InterfaceType> class FakeStreamBase : public InterfaceType {
public:
  void forEach(StreamBase::IterateCallback callback) const override {
    for (const auto& pair : data_) {
      callback(pair.first, pair.second);
    }
  }
  absl::optional<absl::string_view> get(absl::string_view key) const override {
    auto iter = data_.find(key);
    if (iter == data_.end()) {
      return absl::nullopt;
    }
    return absl::make_optional<absl::string_view>(iter->second);
  }
  void set(absl::string_view key, absl::string_view val) override { data_[key] = std::string(val); }
  void erase(absl::string_view key) override { data_.erase(key); }

  // StreamFrame
  FrameFlags frameFlags() const override { return stream_frame_flags_; }

  FrameFlags stream_frame_flags_;

  absl::flat_hash_map<std::string, std::string> data_;
};

/**
 * Fake stream codec factory for test. A simple plain protocol is created for this fake
 * factory. The message format of this protocol is shown below.
 *
 * Fake request message format:
 *   <INT Message Size><Protocol>|<host>|<PATH>|<METHOD>|<key>:<value>;*
 * Fake response message format:
     <INT Message Size><INT Status><Protocol>|<Status Detail>|<key>:<value>;*
 */
class FakeStreamCodecFactory : public CodecFactory {
public:
  class FakeRequest : public FakeStreamBase<Request> {
  public:
    absl::string_view protocol() const override { return protocol_; }
    absl::string_view host() const override { return host_; }
    absl::string_view path() const override { return path_; }
    absl::string_view method() const override { return method_; }

    std::string protocol_;
    std::string host_;
    std::string path_;
    std::string method_;
  };

  class FakeResponse : public FakeStreamBase<Response> {
  public:
    absl::string_view protocol() const override { return protocol_; }
    Status status() const override { return status_; }

    std::string protocol_;
    Status status_;
  };

  class FakeServerCodec : public ServerCodec {
  public:
    bool parseRequestBody() {
      std::string body(message_size_.value(), 0);
      buffer_.copyOut(0, message_size_.value(), body.data());
      buffer_.drain(message_size_.value());
      message_size_.reset();

      std::vector<absl::string_view> result = absl::StrSplit(body, '|');
      if (result.size() != 5) {
        callback_->onDecodingFailure();
        return false;
      }

      auto request = std::make_unique<FakeRequest>();
      request->protocol_ = std::string(result[0]);
      request->host_ = std::string(result[1]);
      request->path_ = std::string(result[2]);
      request->method_ = std::string(result[3]);
      for (absl::string_view pair_str : absl::StrSplit(result[4], ';', absl::SkipEmpty())) {
        auto pair = absl::StrSplit(pair_str, absl::MaxSplits(':', 1));
        request->data_.emplace(pair);
      }
      absl::optional<uint64_t> stream_id;
      bool one_way_stream = false;
      if (auto it = request->data_.find("stream_id"); it != request->data_.end()) {
        stream_id = std::stoull(it->second);
      }
      if (auto it = request->data_.find("one_way"); it != request->data_.end()) {
        one_way_stream = it->second == "true";
      }

      // Mock multiple frames in one request.
      bool end_stream = true;
      if (auto it = request->data_.find("end_stream"); it != request->data_.end()) {
        end_stream = it->second == "true";
      }

      request->stream_frame_flags_ =
          FrameFlags(StreamFlags(stream_id.value_or(0), one_way_stream, false, false), end_stream);

      callback_->onDecodingSuccess(std::move(request));
      return true;
    }

    void setCodecCallbacks(ServerCodecCallbacks& callback) override { callback_ = &callback; }
    void decode(Buffer::Instance& buffer, bool) override {
      buffer_.move(buffer);
      while (true) {
        if (!message_size_.has_value()) {
          if (buffer_.length() < 4) {
            // Wait for more data.
            return;
          }
          // Parsing message size.
          message_size_ = buffer_.peekBEInt<uint32_t>();
          buffer_.drain(4);
        }

        if (buffer_.length() < message_size_.value()) {
          // Wait for more data.
          return;
        }
        // There is enough data to parse a request.
        if (!parseRequestBody()) {
          return;
        }
      }
    }

    void encode(const StreamFrame& response, EncodingCallbacks& callback) override {
      const FakeResponse* typed_response = dynamic_cast<const FakeResponse*>(&response);
      ASSERT(typed_response != nullptr);

      std::string body;
      body.reserve(512);
      body = typed_response->protocol_ + "|" + std::string(typed_response->status_.message()) + "|";
      for (const auto& pair : typed_response->data_) {
        body += pair.first + ":" + pair.second + ";";
      }
      // Additional 4 bytes for status.
      encoding_buffer_.writeBEInt<uint32_t>(body.size() + 4);
      encoding_buffer_.writeBEInt<uint32_t>(
          static_cast<int32_t>(typed_response->status_.raw_code()));
      encoding_buffer_.add(body);

      callback.onEncodingSuccess(encoding_buffer_, response.frameFlags().endStream());
    }

    ResponsePtr respond(Status status, absl::string_view, const Request&) override {
      auto response = std::make_unique<FakeResponse>();
      response->status_ = status;
      response->protocol_ = "fake_protocol_for_test";
      return response;
    }

    absl::optional<uint32_t> message_size_;
    Buffer::OwnedImpl buffer_;
    Buffer::OwnedImpl encoding_buffer_;
    ServerCodecCallbacks* callback_{};
  };

  class FakeClientCodec : public ClientCodec {
  public:
    bool parseResponseBody() {
      int32_t status_code = buffer_.peekBEInt<int32_t>();
      buffer_.drain(4);
      message_size_ = message_size_.value() - 4;

      std::string body(message_size_.value(), 0);
      buffer_.copyOut(0, message_size_.value(), body.data());
      buffer_.drain(message_size_.value());
      message_size_.reset();

      std::vector<absl::string_view> result = absl::StrSplit(body, '|');
      if (result.size() != 3) {
        callback_->onDecodingFailure();
        return false;
      }

      auto response = std::make_unique<FakeResponse>();
      response->status_ = Status(StatusCode(status_code), result[1]);
      response->protocol_ = std::string(result[0]);
      for (absl::string_view pair_str : absl::StrSplit(result[2], ';', absl::SkipEmpty())) {
        auto pair = absl::StrSplit(pair_str, absl::MaxSplits(':', 1));
        response->data_.emplace(pair);
      }

      absl::optional<uint64_t> stream_id;
      bool close_connection = false;
      if (auto it = response->data_.find("stream_id"); it != response->data_.end()) {
        stream_id = std::stoull(it->second);
      }
      if (auto it = response->data_.find("close_connection"); it != response->data_.end()) {
        close_connection = it->second == "true";
      }

      // Mock multiple frames in one response.
      bool end_stream = true;
      if (auto it = response->data_.find("end_stream"); it != response->data_.end()) {
        end_stream = it->second == "true";
      }

      response->stream_frame_flags_ = FrameFlags(
          StreamFlags(stream_id.value_or(0), false, close_connection, false), end_stream);

      callback_->onDecodingSuccess(std::move(response));
      return true;
    }

    void setCodecCallbacks(ClientCodecCallbacks& callback) override { callback_ = &callback; }
    void decode(Buffer::Instance& buffer, bool) override {
      buffer_.move(buffer);
      while (true) {
        if (!message_size_.has_value()) {
          if (buffer_.length() < 4) {
            // Wait for more data.
            return;
          }
          // Parsing message size.
          message_size_ = buffer_.peekBEInt<uint32_t>();
          buffer_.drain(4);

          if (message_size_.value() < 4) {
            callback_->onDecodingFailure();
            return;
          }
        }

        if (buffer_.length() < message_size_.value()) {
          // Wait for more data.
          return;
        }

        // There is enough data to parse a response.
        if (!parseResponseBody()) {
          return;
        }
      }
    }

    void encode(const StreamFrame& request, EncodingCallbacks& callback) override {
      const FakeRequest* typed_request = dynamic_cast<const FakeRequest*>(&request);
      ASSERT(typed_request != nullptr);

      std::string body;
      body.reserve(512);
      body = typed_request->protocol_ + "|" + typed_request->host_ + "|" + typed_request->path_ +
             "|" + typed_request->method_ + "|";
      for (const auto& pair : typed_request->data_) {
        body += pair.first + ":" + pair.second + ";";
      }
      encoding_buffer_.writeBEInt<uint32_t>(body.size());
      encoding_buffer_.add(body);

      callback.onEncodingSuccess(encoding_buffer_, request.frameFlags().endStream());
    }

    absl::optional<uint32_t> message_size_;
    Buffer::OwnedImpl buffer_;
    Buffer::OwnedImpl encoding_buffer_;
    ClientCodecCallbacks* callback_{};
  };

  ServerCodecPtr createServerCodec() const override;
  ClientCodecPtr createClientCodec() const override;
};

class FakeStreamCodecFactoryConfig : public CodecFactoryConfig {
public:
  // CodecFactoryConfig
  CodecFactoryPtr
  createCodecFactory(const Protobuf::Message& config,
                     Envoy::Server::Configuration::FactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::set<std::string> configTypes() override { return {"envoy.generic_proxy.codecs.fake.type"}; }
  std::string name() const override { return "envoy.generic_proxy.codecs.fake"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
