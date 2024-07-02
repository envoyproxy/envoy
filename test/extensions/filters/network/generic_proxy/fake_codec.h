#pragma once

#include <cstdint>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/generic_proxy/access_log.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

template <typename InterfaceType> class FakeStreamBase : public InterfaceType {
public:
  void forEach(HeaderFrame::IterateCallback callback) const override {
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
 *   <INT Message Size>FAKE-REQ|<key>:<value>;*
 * Fake response message format:
 *   <INT Message Size>FAKE-RSP|<key>:<value>;*
 */
class FakeStreamCodecFactory : public CodecFactory {
public:
  class FakeRequest : public FakeStreamBase<RequestHeaderFrame> {
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

  class FakeResponse : public FakeStreamBase<ResponseHeaderFrame> {
  public:
    absl::string_view protocol() const override { return protocol_; }
    StreamStatus status() const override { return status_; }

    std::string protocol_;
    StreamStatus status_;
    std::string message_;
  };

  class FakeCommonFrame : public CommonFrame {
  public:
    // StreamFrame
    FrameFlags frameFlags() const override { return stream_frame_flags_; }
    FrameFlags stream_frame_flags_;
    absl::flat_hash_map<std::string, std::string> data_;
  };

  class FakeServerCodec : public ServerCodec, Logger::Loggable<Logger::Id::filter> {
  public:
    bool parseRequestBody() {
      std::string body(message_size_.value(), 0);
      buffer_.copyOut(0, message_size_.value(), body.data());
      buffer_.drain(message_size_.value());
      message_size_.reset();

      std::vector<absl::string_view> result = absl::StrSplit(body, '|');
      if (result.size() != 2 || result[0] != "FAKE-REQ") {
        callback_->onDecodingFailure();
        return false;
      }

      absl::flat_hash_map<std::string, std::string> data;
      for (absl::string_view pair_str : absl::StrSplit(result[1], ';', absl::SkipEmpty())) {
        auto pair = absl::StrSplit(pair_str, absl::MaxSplits(':', 1));
        data.emplace(pair);
      }

      absl::optional<uint64_t> stream_id;
      bool end_stream = true;
      bool one_way_stream = false;

      if (auto it = data.find("stream_id"); it != data.end()) {
        stream_id = std::stoull(it->second);
        data.erase("stream_id");
      }
      if (auto it = data.find("end_stream"); it != data.end()) {
        end_stream = it->second == "true";
        data.erase("end_stream");
      }
      if (auto it = data.find("one_way"); it != data.end()) {
        one_way_stream = it->second == "true";
        data.erase("one_way");
      }

      uint32_t flags = 0;
      if (end_stream) {
        flags |= FrameFlags::FLAG_END_STREAM;
      }
      if (one_way_stream) {
        flags |= FrameFlags::FLAG_ONE_WAY;
      }

      FrameFlags frame_flags(stream_id.value_or(0), flags);

      const auto it = data.find("message_type");

      if (it == data.end() || it->second == "header") {
        data.erase("message_type");

        auto request = std::make_unique<FakeRequest>();
        request->protocol_ = data["protocol"];
        data.erase("protocol");
        request->host_ = data["host"];
        data.erase("host");
        request->path_ = data["path"];
        data.erase("path");
        request->method_ = data["method"];
        data.erase("method");
        request->data_ = std::move(data);

        request->stream_frame_flags_ = frame_flags;
        callback_->onDecodingSuccess(std::move(request), {});
        return true;
      }

      if (it->second == "common") {
        data.erase("message_type");

        auto request = std::make_unique<FakeCommonFrame>();
        request->data_ = std::move(data);
        request->stream_frame_flags_ = frame_flags;
        callback_->onDecodingSuccess(std::move(request));
        return true;
      }

      callback_->onDecodingFailure();
      return false;
    }

    void setCodecCallbacks(ServerCodecCallbacks& callback) override { callback_ = &callback; }
    void decode(Buffer::Instance& buffer, bool) override {
      ENVOY_LOG(debug, "FakeServerCodec::decode: {}", buffer.toString());

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

    EncodingResult encode(const StreamFrame& response, EncodingContext&) override {
      std::string buffer;
      buffer.reserve(512);
      buffer += "FAKE-RSP|";

      const FakeResponse* typed_response = dynamic_cast<const FakeResponse*>(&response);
      if (typed_response != nullptr) {
        for (const auto& [key, val] : typed_response->data_) {
          buffer += key + ":" + val + ";";
        }

        buffer += "message_type:header;";
        buffer += "protocol:" + typed_response->protocol_ + ";";
        buffer += "status_code:" + std::to_string(typed_response->status_.code()) + ";";
        buffer += "status_message:" + typed_response->message_ + ";";
      } else {
        const FakeCommonFrame* typed_common_response =
            dynamic_cast<const FakeCommonFrame*>(&response);
        ASSERT(typed_common_response != nullptr);

        for (const auto& [key, val] : typed_common_response->data_) {
          buffer += key + ":" + val + ";";
        }
        buffer += "message_type:common;";
      }

      buffer += fmt::format("stream_id:{};", response.frameFlags().streamId());
      buffer += fmt::format("end_stream:{};", response.frameFlags().endStream());
      buffer += fmt::format("close_connection:{};", response.frameFlags().drainClose());

      ENVOY_LOG(debug, "FakeServerCodec::encode: {}", buffer);

      encoding_buffer_.writeBEInt<uint32_t>(buffer.size());
      encoding_buffer_.add(buffer);

      const uint64_t encoded_size = encoding_buffer_.length();

      callback_->writeToConnection(encoding_buffer_);

      return encoded_size;
    }

    ResponsePtr respond(Status status, absl::string_view, const Request&) override {
      auto response = std::make_unique<FakeResponse>();
      response->status_ = {status.raw_code(), status.ok()};
      response->message_ = status.message();
      response->protocol_ = "fake_protocol_for_test";
      return response;
    }

    absl::optional<uint32_t> message_size_;
    Buffer::OwnedImpl buffer_;
    Buffer::OwnedImpl encoding_buffer_;
    ServerCodecCallbacks* callback_{};
  };

  class FakeClientCodec : public ClientCodec, Logger::Loggable<Logger::Id::filter> {
  public:
    bool parseResponseBody() {
      std::string body(message_size_.value(), 0);
      buffer_.copyOut(0, message_size_.value(), body.data());
      buffer_.drain(message_size_.value());
      message_size_.reset();

      std::vector<absl::string_view> result = absl::StrSplit(body, '|');
      if (result.size() != 2 || result[0] != "FAKE-RSP") {
        callback_->onDecodingFailure();
        return false;
      }

      absl::flat_hash_map<std::string, std::string> data;
      for (absl::string_view pair_str : absl::StrSplit(result[1], ';', absl::SkipEmpty())) {
        auto pair = absl::StrSplit(pair_str, absl::MaxSplits(':', 1));
        data.emplace(pair);
      }

      absl::optional<uint64_t> stream_id;
      bool end_stream = true;
      bool close_connection = false;

      if (auto it = data.find("stream_id"); it != data.end()) {
        stream_id = std::stoull(it->second);
        data.erase("stream_id");
      }
      if (auto it = data.find("end_stream"); it != data.end()) {
        end_stream = it->second == "true";
        data.erase("end_stream");
      }
      if (auto it = data.find("close_connection"); it != data.end()) {
        close_connection = it->second == "true";
        data.erase("close_connection");
      }

      uint32_t flags = 0;
      if (end_stream) {
        flags |= FrameFlags::FLAG_END_STREAM;
      }
      if (close_connection) {
        flags |= FrameFlags::FLAG_DRAIN_CLOSE;
      }

      FrameFlags frame_flags(stream_id.value_or(0), flags);

      const auto it = data.find("message_type");

      if (it == data.end() || it->second == "header") {
        data.erase("message_type");

        auto response = std::make_unique<FakeResponse>();
        response->protocol_ = data["protocol"];
        data.erase("protocol");
        response->status_ = {std::stoi(data["status_code"]), std::stoi(data["status_code"]) == 0};
        data.erase("status_code");
        response->message_ = data["status_message"];
        data.erase("status_message");

        response->data_ = std::move(data);
        response->stream_frame_flags_ = frame_flags;
        callback_->onDecodingSuccess(std::move(response), {});
        return true;
      }
      if (it->second == "common") {
        data.erase("message_type");

        auto response = std::make_unique<FakeCommonFrame>();
        response->data_ = std::move(data);
        response->stream_frame_flags_ = frame_flags;
        callback_->onDecodingSuccess(std::move(response));
        return true;
      }

      callback_->onDecodingFailure();
      return false;
    }

    void setCodecCallbacks(ClientCodecCallbacks& callback) override { callback_ = &callback; }
    void decode(Buffer::Instance& buffer, bool) override {
      ENVOY_LOG(debug, "FakeClientCodec::decode: {}", buffer.toString());

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

    EncodingResult encode(const StreamFrame& request, EncodingContext&) override {
      std::string buffer;
      buffer.reserve(512);
      buffer += "FAKE-REQ|";

      const FakeRequest* typed_request = dynamic_cast<const FakeRequest*>(&request);
      if (typed_request != nullptr) {
        for (const auto& [key, val] : typed_request->data_) {
          buffer += key + ":" + val + ";";
        }

        buffer += "message_type:header;";
        buffer += "protocol:" + typed_request->protocol_ + ";";
        buffer += "host:" + typed_request->host_ + ";";
        buffer += "path:" + typed_request->path_ + ";";
        buffer += "method:" + typed_request->method_ + ";";
      } else {
        const FakeCommonFrame* typed_common_request =
            dynamic_cast<const FakeCommonFrame*>(&request);
        ASSERT(typed_common_request != nullptr);

        for (const auto& [key, val] : typed_common_request->data_) {
          buffer += key + ":" + val + ";";
        }
        buffer += "message_type:common;";
      }

      buffer += fmt::format("stream_id:{};", request.frameFlags().streamId());
      buffer += fmt::format("end_stream:{};", request.frameFlags().endStream());
      buffer += fmt::format("one_way:{};", request.frameFlags().oneWayStream());

      ENVOY_LOG(debug, "FakeClientCodec::encode: {}", buffer);

      encoding_buffer_.writeBEInt<uint32_t>(buffer.size());
      encoding_buffer_.add(buffer);

      const uint64_t encoded_size = encoding_buffer_.length();

      callback_->writeToConnection(encoding_buffer_);

      return encoded_size;
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
                     Envoy::Server::Configuration::ServerFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::set<std::string> configTypes() override { return {"envoy.generic_proxy.codecs.fake.type"}; }
  std::string name() const override { return "envoy.generic_proxy.codecs.fake"; }
};

class FakeAccessLogExtensionFilter : public AccessLogFilter {
  bool evaluate(const FormatterContext&, const StreamInfo::StreamInfo&) const override {
    return true;
  }
};

class FakeAccessLogExtensionFilterFactory : public AccessLogFilterFactory {
public:
  // AccessLogFilterFactory
  AccessLogFilterPtr createFilter(const envoy::config::accesslog::v3::ExtensionFilter&,
                                  Server::Configuration::FactoryContext&) override {
    return std::make_unique<FakeAccessLogExtensionFilter>();
  }

  std::set<std::string> configTypes() override {
    return {"envoy.generic_proxy.access_log.fake.type"};
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
  std::string name() const override { return "envoy.generic_proxy.access_log.fake"; }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
