#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/balsa_parser.h"
#include "source/common/http/http1/parser.h"

#include "contrib/envoy/extensions/filters/network/generic_proxy/codecs/http1/v3/http1.pb.h"
#include "contrib/generic_proxy/filters/network/source/interface/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Http1 {

using ProtoConfig =
    envoy::extensions::filters::network::generic_proxy::codecs::http1::v3::Http1CodecConfig;

template <class Interface> class HttpHeaderFrame : public Interface {
public:
  absl::string_view protocol() const override { return "http1"; }
  void forEach(StreamBase::IterateCallback callback) const override {
    headerMap().iterate([cb = std::move(callback)](const Http::HeaderEntry& entry) {
      if (cb(entry.key().getStringView(), entry.value().getStringView())) {
        return Http::HeaderMap::Iterate::Continue;
      }
      return Http::HeaderMap::Iterate::Break;
    });
  };
  absl::optional<absl::string_view> get(absl::string_view key) const override {
    const Http::LowerCaseString lower_key{key};
    const auto entry = headerMap().get(lower_key);
    if (!entry.empty()) {
      return entry[0]->value().getStringView();
    }
    return absl::nullopt;
  }
  void set(absl::string_view key, absl::string_view val) override {
    headerMap().setCopy(Http::LowerCaseString(key), std::string(val));
  }
  void erase(absl::string_view key) override { headerMap().remove(Http::LowerCaseString(key)); }

  FrameFlags frameFlags() const override { return frame_flags_; }

  virtual Http::HeaderMap& headerMap() const PURE;

protected:
  FrameFlags frame_flags_;
};

class HttpRequestFrame : public HttpHeaderFrame<StreamRequest> {
public:
  HttpRequestFrame(Http::RequestHeaderMapPtr request, bool end_stream)
      : request_(std::move(request)) {
    ASSERT(request_ != nullptr);
    frame_flags_ = {StreamFlags{}, end_stream};
  }

  absl::string_view host() const override { return request_->getHostValue(); }
  absl::string_view path() const override { return request_->getPathValue(); }
  absl::string_view method() const override { return request_->getMethodValue(); }

  Http::HeaderMap& headerMap() const override { return *request_; }
  Http::RequestHeaderMapPtr request_;
};

class HttpResponseFrame : public HttpHeaderFrame<StreamResponse> {
public:
  HttpResponseFrame(Http::ResponseHeaderMapPtr response, bool end_stream)
      : response_(std::move(response)) {
    ASSERT(response_ != nullptr);

    const bool drain_close = Envoy::StringUtil::caseFindToken(
        response_->getConnectionValue(), ",", Http::Headers::get().ConnectionValues.Close);

    frame_flags_ = {StreamFlags{0, false, drain_close, false}, end_stream};
  }

  StreamStatus status() const override {
    int32_t status = 0;
    if (absl::SimpleAtoi(response_->getStatusValue(), &status)) {
      return {status, status < 500 && status >= 100};
    }
    // Unknown HTTP status code.
    return {-1, false};
  }

  Http::HeaderMap& headerMap() const override { return *response_; }

  Http::ResponseHeaderMapPtr response_;
};

class HttpRawBodyFrame : public StreamFrame {
public:
  HttpRawBodyFrame(Envoy::Buffer::Instance& buffer, bool end_stream)
      : frame_flags_({StreamFlags{}, end_stream}) {
    buffer_.move(buffer);
  }

  FrameFlags frameFlags() const override { return frame_flags_; }

  uint64_t length() const { return buffer_.length(); }
  void moveTo(Envoy::Buffer::Instance& t) const { t.move(buffer_); }
  std::string toString() const { return buffer_.toString(); }

private:
  mutable Buffer::OwnedImpl buffer_;
  const FrameFlags frame_flags_;
};

class Utility {
public:
  static absl::Status encodeHeaders(Buffer::Instance& buffer, const Http::RequestHeaderMap& headers,
                                    bool chunk_encoding);
  static absl::Status encodeHeaders(Buffer::Instance& buffer,
                                    const Http::ResponseHeaderMap& headers, bool chunk_encoding);
  static void encodeBody(Buffer::Instance& buffer, const HttpRawBodyFrame& body,
                         bool chunk_encoding, bool end_stream);

  static absl::Status validateHeaders(const Http::RequestHeaderMap& headers);
  static absl::Status validateHeaders(const Http::ResponseHeaderMap& headers);

  static bool isChunked(const Http::RequestOrResponseHeaderMap& headers, bool bodiless_message);

  static uint64_t statusToHttpStatus(absl::StatusCode status_code);
};

class HttpCodecBase : public Http::Http1::ParserCallbacks,
                      public Envoy::Logger::Loggable<Envoy::Logger::Id::http> {
public:
  // ParserCallbacks.
  Http::Http1::CallbackResult onMessageBegin() override {
    header_parsing_state_ = HeaderParsingState::Field;
    return onMessageBeginImpl();
  }
  Http::Http1::CallbackResult onUrl(const char* data, size_t length) override {
    onUrlImpl(data, length);
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onStatus(const char* data, size_t length) override {
    onStatusImpl(data, length);
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeaderField(const char* data, size_t length) override {
    onHeaderFieldImpl(data, length);
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeaderValue(const char* data, size_t length) override {
    onHeaderValueImpl(data, length);
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeadersComplete() override {
    completeCurrentHeader();
    return onHeadersCompleteImpl();
  }
  void bufferBody(const char* data, size_t length) override { buffered_body_.add(data, length); }
  Http::Http1::CallbackResult onMessageComplete() override {
    onMessageCompleteImpl();
    return Http::Http1::CallbackResult::Success;
  }
  void onChunkHeader(bool is_final_chunk) override {
    if (is_final_chunk) {
      dispatchBufferedBody(false);
    }
  }

  virtual Http::Http1::CallbackResult onMessageBeginImpl() PURE;
  virtual void onUrlImpl(const char* data, size_t length) PURE;
  virtual void onStatusImpl(const char* data, size_t length) PURE;
  void onHeaderFieldImpl(const char* data, size_t length) {
    if (header_parsing_state_ == HeaderParsingState::Done) {
      // Ignore trailers for now.
      return;
    }

    if (header_parsing_state_ == HeaderParsingState::Value) {
      completeCurrentHeader();
    }

    header_parsing_state_ = HeaderParsingState::Field;
    current_header_field_.append(data, length);
  }
  void onHeaderValueImpl(const char* data, size_t length) {
    if (header_parsing_state_ == HeaderParsingState::Done) {
      // Ignore trailers for now.
      return;
    }

    header_parsing_state_ = HeaderParsingState::Value;
    absl::string_view value(data, length);
    if (current_header_value_.empty()) {
      value = StringUtil::ltrim(value);
    }

    current_header_value_.append(value.data(), value.size());
  }
  virtual Http::Http1::CallbackResult onHeadersCompleteImpl() PURE;
  virtual void onMessageCompleteImpl() PURE;

  void completeCurrentHeader() {
    current_header_value_.rtrim();
    current_header_field_.inlineTransform([](char c) { return absl::ascii_tolower(c); });
    headerMap().addViaMove(std::move(current_header_field_), std::move(current_header_value_));

    ASSERT(current_header_field_.empty());
    ASSERT(current_header_value_.empty());
  }

  bool decodeBuffer(Buffer::Instance& buffer) {
    decode_buffer_.move(buffer);

    // Always resume before decoding.
    parser_->resume();

    while (decode_buffer_.length() > 0) {
      const auto slice = decode_buffer_.frontSlice();
      const auto nread = parser_->execute(static_cast<const char*>(slice.mem_), slice.len_);
      decode_buffer_.drain(nread);

      const auto status = parser_->getStatus();
      if (status == Http::Http1::ParserStatus::Paused) {
        return true;
      }
      if (status != Http::Http1::ParserStatus::Ok) {
        // Decoding error.
        return false;
      }
      if (nread == 0) {
        // No more data to read and parser is not paused, break to avoid infinite loop.
        break;
      }
    }
    // Try to dispatch any buffered body. If the message is complete then this will be a no-op.
    dispatchBufferedBody(false);
    return true;
  }

  virtual Http::HeaderMap& headerMap() PURE;
  virtual void dispatchBufferedBody(bool end_stream) PURE;

protected:
  enum class HeaderParsingState { Field, Value, Done };
  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};

  Http::HeaderString current_header_field_;
  Http::HeaderString current_header_value_;

  Buffer::OwnedImpl decode_buffer_;
  Buffer::OwnedImpl buffered_body_;

  Http::Http1::ParserPtr parser_;
};

class Http1ServerCodec : public HttpCodecBase, public ServerCodec {
public:
  Http1ServerCodec() {
    parser_ = Http::Http1::ParserPtr{new Http::Http1::BalsaParser(Http::Http1::MessageType::Request,
                                                                  this, 64 * 1024, false, false)};
  }

  Http::Http1::CallbackResult onMessageBeginImpl() override {
    if (active_request_) {
      ENVOY_LOG(
          error,
          "Generic proxy HTTP1 codec: multiple requests on the same connection at same time.");
      return Http::Http1::CallbackResult::Error;
    }
    active_request_ = true;

    request_headers_ = Http::RequestHeaderMapImpl::create();
    return Http::Http1::CallbackResult::Success;
  }
  void onUrlImpl(const char* data, size_t length) override {
    request_headers_->setPath(absl::string_view(data, length));
  }
  void onStatusImpl(const char*, size_t) override {}
  Http::Http1::CallbackResult onHeadersCompleteImpl() override {
    request_headers_->setMethod(parser_->methodName());

    if (!parser_->isHttp11()) {
      ENVOY_LOG(error,
                "Generic proxy HTTP1 codec: unsupported HTTP version, only HTTP/1.1 is supported.");
      return Http::Http1::CallbackResult::Error;
    }

    // Validate request headers.
    const auto validate_headers_status = Utility::validateHeaders(*request_headers_);
    if (!validate_headers_status.ok()) {
      ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to validate request headers: {}",
                validate_headers_status.message());
      return Http::Http1::CallbackResult::Error;
    }

    // Upgrade requests have been rejected by above validation and only handle chunked request
    // or content-length > 0.
    const bool non_end_stream = parser_->isChunked() || parser_->contentLength().value_or(0) > 0;

    ENVOY_LOG(debug, "decoding request headers complete (end_stream={}):\n{}", !non_end_stream,
              *request_headers_);

    if (non_end_stream) {
      auto request = std::make_unique<HttpRequestFrame>(std::move(request_headers_), false);
      onDecodingSuccess(std::move(request));
    } else {
      deferred_end_stream_headers_ = true;
    }

    return Http::Http1::CallbackResult::Success;
  }
  void onMessageCompleteImpl() override {
    if (deferred_end_stream_headers_) {
      deferred_end_stream_headers_ = false;
      auto request = std::make_unique<HttpRequestFrame>(std::move(request_headers_), true);
      onDecodingSuccess(std::move(request));
    } else {
      dispatchBufferedBody(true);
    }
    parser_->pause();
  }
  Http::HeaderMap& headerMap() override { return *request_headers_; }
  void dispatchBufferedBody(bool end_stream) override {
    if (buffered_body_.length() > 0 || end_stream) {
      ENVOY_LOG(debug, "decoding request body (end_stream={} size={})", end_stream,
                buffered_body_.length());
      auto request = std::make_unique<HttpRawBodyFrame>(buffered_body_, end_stream);
      onDecodingSuccess(std::move(request));
    }
  }

  void setCodecCallbacks(ServerCodecCallbacks& callbacks) override { callbacks_ = &callbacks; }
  void decode(Envoy::Buffer::Instance& buffer, bool) override {
    if (!decodeBuffer(buffer)) {
      callbacks_->onDecodingFailure();
    }
  }
  void encode(const StreamFrame& frame, EncodingCallbacks& callbacks) override;
  ResponsePtr respond(absl::Status status, absl::string_view, const Request&) override {
    auto response = Http::ResponseHeaderMapImpl::create();
    response->setStatus(std::to_string(Utility::statusToHttpStatus(status.code())));
    response->setContentLength(0);
    response->addCopy(Http::LowerCaseString("reason"), status.message());
    return std::make_unique<HttpResponseFrame>(std::move(response), true);
  }

  void onDecodingSuccess(StreamFramePtr&& frame) {
    if (!callbacks_->connection().has_value()) {
      return;
    }

    callbacks_->onDecodingSuccess(std::move(frame));

    // Connection may have been closed by the callback.
    auto connection_state = callbacks_->connection().has_value()
                                ? callbacks_->connection()->state()
                                : Network::Connection::State::Closed;
    if (connection_state != Network::Connection::State::Open) {
      parser_->pause();
    }
  }

  Envoy::Buffer::OwnedImpl response_buffer_;
  Http::RequestHeaderMapPtr request_headers_;
  bool deferred_end_stream_headers_{};
  bool chunk_encoding_{};

  // HTTP1.1 only supports a single request/response per connection at same time.
  bool active_request_{};

  ServerCodecCallbacks* callbacks_{};
};

class Http1ClientCodec : public HttpCodecBase, public ClientCodec {
public:
  Http1ClientCodec() {
    parser_ = Http::Http1::ParserPtr{new Http::Http1::BalsaParser(
        Http::Http1::MessageType::Response, this, 64 * 1024, false, false)};
  }

  Http::Http1::CallbackResult onMessageBeginImpl() override {
    response_headers_ = Http::ResponseHeaderMapImpl::create();
    return Http::Http1::CallbackResult::Success;
  }
  void onUrlImpl(const char*, size_t) override {}
  void onStatusImpl(const char*, size_t) override {}
  Http::Http1::CallbackResult onHeadersCompleteImpl() override {
    response_headers_->setStatus(std::to_string(static_cast<uint16_t>(parser_->statusCode())));

    // Validate response headers.
    const auto validate_headers_status = Utility::validateHeaders(*response_headers_);
    if (!validate_headers_status.ok()) {
      ENVOY_LOG(error, "Generic proxy HTTP1 codec: failed to validate response headers: {}",
                validate_headers_status.message());
      return Http::Http1::CallbackResult::Error;
    }

    const bool non_end_stream = parser_->isChunked() || parser_->contentLength().value_or(0) > 0;

    ENVOY_LOG(debug, "decoding response headers complete (end_stream={}):\n{}", !non_end_stream,
              *response_headers_);

    if (non_end_stream) {
      auto request = std::make_unique<HttpResponseFrame>(std::move(response_headers_), false);
      onDecodingSuccess(std::move(request));
    } else {
      deferred_end_stream_headers_ = true;
    }

    return Http::Http1::CallbackResult::Success;
  }
  void onMessageCompleteImpl() override {
    if (deferred_end_stream_headers_) {
      deferred_end_stream_headers_ = false;
      auto request = std::make_unique<HttpResponseFrame>(std::move(response_headers_), true);
      onDecodingSuccess(std::move(request));
    } else {
      dispatchBufferedBody(true);
    }
    parser_->pause();
  }
  Http::HeaderMap& headerMap() override { return *response_headers_; }
  void dispatchBufferedBody(bool end_stream) override {
    if (buffered_body_.length() > 0 || end_stream) {
      auto request = std::make_unique<HttpRawBodyFrame>(buffered_body_, end_stream);
      onDecodingSuccess(std::move(request));
    }
  }

  void setCodecCallbacks(ClientCodecCallbacks& callbacks) override { callbacks_ = &callbacks; }
  void decode(Envoy::Buffer::Instance& buffer, bool) override {
    if (!decodeBuffer(buffer)) {
      callbacks_->onDecodingFailure();
    }
  }
  void encode(const StreamFrame& frame, EncodingCallbacks& callbacks) override;

  void onDecodingSuccess(StreamFramePtr&& frame) {
    if (!callbacks_->connection().has_value()) {
      return;
    }

    callbacks_->onDecodingSuccess(std::move(frame));

    // Connection may have been closed by the callback.
    auto connection_state = callbacks_->connection().has_value()
                                ? callbacks_->connection()->state()
                                : Network::Connection::State::Closed;
    if (connection_state != Network::Connection::State::Open) {
      parser_->pause();
    }
  }

  Envoy::Buffer::OwnedImpl request_buffer_;
  Http::ResponseHeaderMapPtr response_headers_;
  bool deferred_end_stream_headers_{};
  bool chunk_encoding_{};

  ClientCodecCallbacks* callbacks_{};
};

class HttpCodecFactory : public CodecFactory {
public:
  ClientCodecPtr createClientCodec() const override { return std::make_unique<Http1ClientCodec>(); }

  ServerCodecPtr createServerCodec() const override { return std::make_unique<Http1ServerCodec>(); }
};

class HttpCodecFactoryConfig : public CodecFactoryConfig {
public:
  // CodecFactoryConfig
  CodecFactoryPtr
  createCodecFactory(const Envoy::Protobuf::Message& config,
                     Envoy::Server::Configuration::FactoryContext& context) override;
  std::string name() const override { return "envoy.generic_proxy.codecs.http1"; }
  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoConfig>();
  }
};

} // namespace Http1
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
