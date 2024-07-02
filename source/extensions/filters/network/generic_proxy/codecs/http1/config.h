#pragma once

#include "envoy/extensions/filters/network/generic_proxy/codecs/http1/v3/http1.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/http1/balsa_parser.h"
#include "source/common/http/http1/parser.h"
#include "source/extensions/filters/network/generic_proxy/interface/codec.h"

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
  void forEach(HeaderFrame::IterateCallback callback) const override {
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

  virtual Http::RequestOrResponseHeaderMap& headerMap() const PURE;

  // Optional buffer for the raw body. This is only make sense for local response and
  // request/responses in single frame mode.
  Buffer::Instance& optionalBuffer() const { return buffer_; }

protected:
  FrameFlags frame_flags_;
  mutable Envoy::Buffer::OwnedImpl buffer_;
};

class HttpRequestFrame : public HttpHeaderFrame<StreamRequest> {
public:
  HttpRequestFrame(Http::RequestHeaderMapPtr request, bool end_stream)
      : request_(std::move(request)) {
    ASSERT(request_ != nullptr);
    frame_flags_ = {0, end_stream ? FrameFlags::FLAG_END_STREAM : FrameFlags::FLAG_EMPTY};
  }

  absl::string_view host() const override { return request_->getHostValue(); }
  absl::string_view path() const override { return request_->getPathValue(); }
  absl::string_view method() const override { return request_->getMethodValue(); }

  Http::RequestOrResponseHeaderMap& headerMap() const override { return *request_; }
  Http::RequestHeaderMapPtr request_;
};

class HttpResponseFrame : public HttpHeaderFrame<StreamResponse> {
public:
  HttpResponseFrame(Http::ResponseHeaderMapPtr response, bool end_stream)
      : response_(std::move(response)) {
    ASSERT(response_ != nullptr);

    const bool drain_close = Envoy::StringUtil::caseFindToken(
        response_->getConnectionValue(), ",", Http::Headers::get().ConnectionValues.Close);

    uint32_t flags = 0;
    if (end_stream) {
      flags |= FrameFlags::FLAG_END_STREAM;
    }
    if (drain_close) {
      flags |= FrameFlags::FLAG_DRAIN_CLOSE;
    }

    frame_flags_ = {0, flags};
  }

  StreamStatus status() const override {
    auto status_view = response_->getStatusValue();
    int32_t status = 0;
    if (absl::SimpleAtoi(status_view, &status)) {
      return {status, status < 500 && status > 99};
    }
    // Unknown HTTP status. Return -1 and false.
    return {-1, false};
  }

  Http::RequestOrResponseHeaderMap& headerMap() const override { return *response_; }

  Http::ResponseHeaderMapPtr response_;
};

class HttpRawBodyFrame : public CommonFrame {
public:
  HttpRawBodyFrame(Envoy::Buffer::Instance& buffer, bool end_stream)
      : frame_flags_(0, end_stream ? FrameFlags::FLAG_END_STREAM : FrameFlags::FLAG_EMPTY) {
    buffer_.move(buffer);
  }
  FrameFlags frameFlags() const override { return frame_flags_; }

  Buffer::Instance& buffer() const { return buffer_; }

private:
  mutable Buffer::OwnedImpl buffer_;
  const FrameFlags frame_flags_;
};

class Utility {
public:
  static absl::Status encodeRequestHeaders(Buffer::Instance& buffer,
                                           const Http::RequestHeaderMap& headers,
                                           bool chunk_encoding);
  static absl::Status encodeResponseHeaders(Buffer::Instance& buffer,
                                            const Http::ResponseHeaderMap& headers,
                                            bool chunk_encoding);
  static void encodeBody(Buffer::Instance& dst_buffer, Buffer::Instance& src_buffer,
                         bool chunk_encoding, bool end_stream);

  static absl::Status validateRequestHeaders(Http::RequestHeaderMap& headers);
  static absl::Status validateResponseHeaders(Http::ResponseHeaderMap& headers,
                                              Envoy::Http::Code code);

  static bool isChunked(const Http::RequestOrResponseHeaderMap& headers, bool bodiless);

  static bool hasBody(const Envoy::Http::Http1::Parser& parser, bool response,
                      bool response_for_head_request);

  static uint64_t statusToHttpStatus(absl::StatusCode status_code);
};

// ActiveRequest is used to store the state of server codec for decoding a request
// and encoding a response.
struct ActiveRequest {
  Http::RequestHeaderMapPtr request_headers_;
  bool request_has_body_{};

  bool request_complete_{};
  bool response_chunk_encoding_{};
};

// ExpectResponse is used to store the state of client codec for decoding a response
// and encoding a request.
struct ExpectResponse {
  Http::ResponseHeaderMapPtr response_headers_;
  bool response_has_body_{};

  bool request_complete_{};
  bool head_request_{};
  bool request_chunk_encoding_{};
};

class Http1CodecBase : public Http::Http1::ParserCallbacks,
                       public Envoy::Logger::Loggable<Envoy::Logger::Id::http> {
public:
  Http1CodecBase(bool single_frame_mode, uint32_t max_buffer_size, bool server_codec)
      : single_frame_mode_(single_frame_mode), max_buffer_size_(max_buffer_size) {
    if (server_codec) {
      parser_ = Http::Http1::ParserPtr{new Http::Http1::BalsaParser(
          Http::Http1::MessageType::Request, this, 64 * 1024, false, false)};
    } else {
      parser_ = Http::Http1::ParserPtr{new Http::Http1::BalsaParser(
          Http::Http1::MessageType::Response, this, 64 * 1024, false, false)};
    }
  }

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
    if (header_parsing_state_ == HeaderParsingState::Done) {
      // Ignore trailers for now.
      return Http::Http1::CallbackResult::Success;
    }
    if (header_parsing_state_ == HeaderParsingState::Value) {
      completeCurrentHeader();
    }
    current_header_field_.append(data, length);

    header_parsing_state_ = HeaderParsingState::Field;
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeaderValue(const char* data, size_t length) override {
    if (header_parsing_state_ == HeaderParsingState::Done) {
      // Ignore trailers for now.
      return Http::Http1::CallbackResult::Success;
    }

    absl::string_view value(data, length);
    if (current_header_value_.empty()) {
      value = StringUtil::ltrim(value);
    }

    current_header_value_.append(value.data(), value.size());

    header_parsing_state_ = HeaderParsingState::Value;
    return Http::Http1::CallbackResult::Success;
  }
  Http::Http1::CallbackResult onHeadersComplete() override {
    completeCurrentHeader();
    header_parsing_state_ = HeaderParsingState::Done;
    return onHeadersCompleteImpl();
  }
  void bufferBody(const char* data, size_t length) override { buffered_body_.add(data, length); }
  Http::Http1::CallbackResult onMessageComplete() override { return onMessageCompleteImpl(); }
  void onChunkHeader(bool is_final_chunk) override {
    if (is_final_chunk) {
      dispatchBufferedBody(false);
    }
  }

  virtual Http::Http1::CallbackResult onMessageBeginImpl() PURE;
  virtual void onUrlImpl(const char* data, size_t length) PURE;
  virtual void onStatusImpl(const char* data, size_t length) PURE;
  virtual Http::Http1::CallbackResult onHeadersCompleteImpl() PURE;
  virtual Http::Http1::CallbackResult onMessageCompleteImpl() PURE;

  void completeCurrentHeader() {
    current_header_value_.rtrim();
    current_header_field_.inlineTransform([](char c) { return absl::ascii_tolower(c); });
    headerMap().addViaMove(std::move(current_header_field_), std::move(current_header_value_));

    ASSERT(current_header_field_.empty());
    ASSERT(current_header_value_.empty());
  }

  bool decodeBuffer(Buffer::Instance& buffer);

  bool dispatchBufferedBody(bool end_stream);
  bool bufferedBodyOverflow();

  virtual Http::HeaderMap& headerMap() PURE;

  virtual void onDecodingSuccess(RequestHeaderFramePtr request_header_frame,
                                 absl::optional<StartTime> start_time) PURE;
  virtual void onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                                 absl::optional<StartTime> start_time) PURE;
  virtual void onDecodingSuccess(CommonFramePtr frame) PURE;
  virtual void onDecodingFailure() PURE;

protected:
  enum class HeaderParsingState { Field, Value, Done };

  Envoy::Buffer::OwnedImpl decoding_buffer_;
  Envoy::Buffer::OwnedImpl encoding_buffer_;

  Buffer::OwnedImpl buffered_body_;

  Http::Http1::ParserPtr parser_;
  Http::HeaderString current_header_field_;
  Http::HeaderString current_header_value_;
  HeaderParsingState header_parsing_state_{HeaderParsingState::Field};

  const bool single_frame_mode_{};
  const uint32_t max_buffer_size_{};

  bool deferred_end_stream_headers_{};
};

class Http1ServerCodec : public Http1CodecBase, public ServerCodec {
public:
  Http1ServerCodec(bool single_frame_mode, uint32_t max_buffer_size)
      : Http1CodecBase(single_frame_mode, max_buffer_size, true) {}

  Http::Http1::CallbackResult onMessageBeginImpl() override;
  void onUrlImpl(const char* data, size_t length) override {
    ASSERT(active_request_.has_value());
    ASSERT(active_request_->request_headers_ != nullptr);
    active_request_->request_headers_->setPath(absl::string_view(data, length));
  }
  void onStatusImpl(const char*, size_t) override {}
  Http::Http1::CallbackResult onHeadersCompleteImpl() override;
  Http::Http1::CallbackResult onMessageCompleteImpl() override;

  Http::HeaderMap& headerMap() override { return *active_request_->request_headers_; }

  void setCodecCallbacks(ServerCodecCallbacks& callbacks) override { callbacks_ = &callbacks; }
  void decode(Envoy::Buffer::Instance& buffer, bool) override {
    if (!decodeBuffer(buffer)) {
      onDecodingFailure();
    }
  }
  EncodingResult encode(const StreamFrame& frame, EncodingContext& ctx) override;
  ResponsePtr respond(absl::Status status, absl::string_view data, const Request&) override {
    auto response = Http::ResponseHeaderMapImpl::create();
    response->setStatus(std::to_string(Utility::statusToHttpStatus(status.code())));
    response->setContentLength(data.size());
    response->addCopy(Http::LowerCaseString("reason"), status.message());
    auto response_frame = std::make_unique<HttpResponseFrame>(std::move(response), true);
    response_frame->optionalBuffer().add(data.data(), data.size());
    return response_frame;
  }

  void onDecodingSuccess(ResponseHeaderFramePtr, absl::optional<StartTime>) override {}
  void onDecodingSuccess(RequestHeaderFramePtr request_header_frame,
                         absl::optional<StartTime> start_time) override {
    if (callbacks_->connection().has_value()) {
      callbacks_->onDecodingSuccess(std::move(request_header_frame), std::move(start_time));
    }

    // Connection may have been closed by the callback.
    if (!callbacks_->connection().has_value() ||
        callbacks_->connection()->state() != Network::Connection::State::Open) {
      parser_->pause();
    }
  }
  void onDecodingSuccess(CommonFramePtr frame) override {
    if (callbacks_->connection().has_value()) {
      callbacks_->onDecodingSuccess(std::move(frame));
    }

    // Connection may have been closed by the callback.
    if (!callbacks_->connection().has_value() ||
        callbacks_->connection()->state() != Network::Connection::State::Open) {
      parser_->pause();
    }
  }
  // TODO(wbpcode): send 400 bad request to client as response and then call the callback.
  void onDecodingFailure() override { callbacks_->onDecodingFailure(); }

  absl::optional<ActiveRequest> active_request_;
  ServerCodecCallbacks* callbacks_{};
};

class Http1ClientCodec : public Http1CodecBase, public ClientCodec {
public:
  Http1ClientCodec(bool single_frame_mode, uint32_t max_buffer_size)
      : Http1CodecBase(single_frame_mode, max_buffer_size, false) {}

  Http::Http1::CallbackResult onMessageBeginImpl() override;
  void onUrlImpl(const char*, size_t) override {}
  void onStatusImpl(const char*, size_t) override {}
  Http::Http1::CallbackResult onHeadersCompleteImpl() override;
  Http::Http1::CallbackResult onMessageCompleteImpl() override;

  Http::HeaderMap& headerMap() override { return *expect_response_->response_headers_; }

  void setCodecCallbacks(ClientCodecCallbacks& callbacks) override { callbacks_ = &callbacks; }
  void decode(Envoy::Buffer::Instance& buffer, bool) override {
    if (!decodeBuffer(buffer)) {
      onDecodingFailure();
    }
  }
  EncodingResult encode(const StreamFrame& frame, EncodingContext& ctx) override;

  void onDecodingSuccess(RequestHeaderFramePtr, absl::optional<StartTime>) override {}
  void onDecodingSuccess(ResponseHeaderFramePtr response_header_frame,
                         absl::optional<StartTime> start_time) override {
    if (callbacks_->connection().has_value()) {
      callbacks_->onDecodingSuccess(std::move(response_header_frame), std::move(start_time));
    }

    // Connection may have been closed by the callback.
    if (!callbacks_->connection().has_value() ||
        callbacks_->connection()->state() != Network::Connection::State::Open) {
      parser_->pause();
    }
  }
  void onDecodingSuccess(CommonFramePtr frame) override {
    if (callbacks_->connection().has_value()) {
      callbacks_->onDecodingSuccess(std::move(frame));
    }

    // Connection may have been closed by the callback.
    if (!callbacks_->connection().has_value() ||
        callbacks_->connection()->state() != Network::Connection::State::Open) {
      parser_->pause();
    }
  }
  void onDecodingFailure() override { callbacks_->onDecodingFailure(); }

  absl::optional<ExpectResponse> expect_response_;

  ClientCodecCallbacks* callbacks_{};
};

class Http1CodecFactory : public CodecFactory {
public:
  Http1CodecFactory(bool single_frame_mode, uint32_t max_buffer_size)
      : single_frame_mode_(single_frame_mode), max_buffer_size_(max_buffer_size) {}

  ClientCodecPtr createClientCodec() const override {
    return std::make_unique<Http1ClientCodec>(single_frame_mode_, max_buffer_size_);
  }

  ServerCodecPtr createServerCodec() const override {
    return std::make_unique<Http1ServerCodec>(single_frame_mode_, max_buffer_size_);
  }

private:
  const bool single_frame_mode_{};
  const uint32_t max_buffer_size_{};
};

class Http1CodecFactoryConfig : public CodecFactoryConfig {
public:
  // CodecFactoryConfig
  CodecFactoryPtr
  createCodecFactory(const Envoy::Protobuf::Message& config,
                     Envoy::Server::Configuration::ServerFactoryContext& context) override;
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
