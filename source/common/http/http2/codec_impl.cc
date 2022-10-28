#include "source/common/http/http2/codec_impl.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/exception.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/http2/codec_stats.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/cleanup/cleanup.h"
#include "absl/container/fixed_array.h"
#include "quiche/http2/adapter/callback_visitor.h"
#include "quiche/http2/adapter/nghttp2_adapter.h"
#include "quiche/http2/adapter/oghttp2_adapter.h"

namespace Envoy {
namespace Http {
namespace Http2 {

// Changes or additions to details should be reflected in
// docs/root/configuration/http/http_conn_man/response_code_details.rst
class Http2ResponseCodeDetailValues {
public:
  // Invalid HTTP header field was received and stream is going to be
  // closed.
  const absl::string_view ng_http2_err_http_header_ = "http2.invalid.header.field";
  // Violation in HTTP messaging rule.
  const absl::string_view ng_http2_err_http_messaging_ = "http2.violation.of.messaging.rule";
  // none of the above
  const absl::string_view ng_http2_err_unknown_ = "http2.unknown.nghttp2.error";
  // The number of headers (or trailers) exceeded the configured limits
  const absl::string_view too_many_headers = "http2.too_many_headers";
  // Envoy detected an HTTP/2 frame flood from the server.
  const absl::string_view outbound_frame_flood = "http2.outbound_frames_flood";
  // Envoy detected an inbound HTTP/2 frame flood.
  const absl::string_view inbound_empty_frame_flood = "http2.inbound_empty_frames_flood";
  // Envoy was configured to drop requests with header keys beginning with underscores.
  const absl::string_view invalid_underscore = "http2.unexpected_underscore";
  // The peer refused the stream.
  const absl::string_view remote_refused = "http2.remote_refuse";
  // The peer reset the stream.
  const absl::string_view remote_reset = "http2.remote_reset";

  const absl::string_view errorDetails(int error_code) const {
    switch (error_code) {
    case NGHTTP2_ERR_HTTP_HEADER:
      return ng_http2_err_http_header_;
    case NGHTTP2_ERR_HTTP_MESSAGING:
      return ng_http2_err_http_messaging_;
    default:
      return ng_http2_err_unknown_;
    }
  }
};

int reasonToReset(StreamResetReason reason) {
  switch (reason) {
  case StreamResetReason::LocalRefusedStreamReset:
    return NGHTTP2_REFUSED_STREAM;
  case StreamResetReason::ConnectError:
    return NGHTTP2_CONNECT_ERROR;
  default:
    return NGHTTP2_NO_ERROR;
  }
}

using Http2ResponseCodeDetails = ConstSingleton<Http2ResponseCodeDetailValues>;

ReceivedSettingsImpl::ReceivedSettingsImpl(const nghttp2_settings& settings) {
  for (uint32_t i = 0; i < settings.niv; ++i) {
    if (settings.iv[i].settings_id == NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS) {
      concurrent_stream_limit_ = settings.iv[i].value;
      break;
    }
  }
}

bool Utility::reconstituteCrumbledCookies(const HeaderString& key, const HeaderString& value,
                                          HeaderString& cookies) {
  if (key != Headers::get().Cookie.get().c_str()) {
    return false;
  }

  if (!cookies.empty()) {
    cookies.append("; ", 2);
  }

  const absl::string_view value_view = value.getStringView();
  cookies.append(value_view.data(), value_view.size());
  return true;
}

ConnectionImpl::Http2Callbacks ConnectionImpl::http2_callbacks_;

std::unique_ptr<http2::adapter::Http2Adapter>
ProdNghttp2SessionFactory::create(const nghttp2_session_callbacks* callbacks,
                                  ConnectionImpl* connection,
                                  const http2::adapter::OgHttp2Adapter::Options& options) {
  auto visitor = std::make_unique<http2::adapter::CallbackVisitor>(
      http2::adapter::Perspective::kClient, *callbacks, connection);
  std::unique_ptr<http2::adapter::Http2Adapter> adapter =
      http2::adapter::OgHttp2Adapter::Create(*visitor, options);
  connection->setVisitor(std::move(visitor));
  return adapter;
}

std::unique_ptr<http2::adapter::Http2Adapter>
ProdNghttp2SessionFactory::create(const nghttp2_session_callbacks* callbacks,
                                  ConnectionImpl* connection, const nghttp2_option* options) {
  auto visitor = std::make_unique<http2::adapter::CallbackVisitor>(
      http2::adapter::Perspective::kClient, *callbacks, connection);
  auto adapter = http2::adapter::NgHttp2Adapter::CreateClientAdapter(*visitor, options);
  auto stream_close_listener = [p = adapter.get()](http2::adapter::Http2StreamId stream_id) {
    p->RemoveStream(stream_id);
  };
  visitor->set_stream_close_listener(std::move(stream_close_listener));
  connection->setVisitor(std::move(visitor));
  return adapter;
}

void ProdNghttp2SessionFactory::init(ConnectionImpl* connection,
                                     const envoy::config::core::v3::Http2ProtocolOptions& options) {
  connection->sendSettings(options, true);
}

/**
 * Helper to remove const during a cast. nghttp2 takes non-const pointers for headers even though
 * it copies them.
 */
template <typename T> static T* removeConst(const void* object) {
  return const_cast<T*>(reinterpret_cast<const T*>(object));
}

ConnectionImpl::StreamImpl::StreamImpl(ConnectionImpl& parent, uint32_t buffer_limit)
    : MultiplexedStreamImplBase(parent.connection_.dispatcher()), parent_(parent),
      pending_recv_data_(parent_.connection_.dispatcher().getWatermarkFactory().createBuffer(
          [this]() -> void { this->pendingRecvBufferLowWatermark(); },
          [this]() -> void { this->pendingRecvBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      pending_send_data_(parent_.connection_.dispatcher().getWatermarkFactory().createBuffer(
          [this]() -> void { this->pendingSendBufferLowWatermark(); },
          [this]() -> void { this->pendingSendBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      local_end_stream_sent_(false), remote_end_stream_(false), data_deferred_(false),
      received_noninformational_headers_(false),
      pending_receive_buffer_high_watermark_called_(false),
      pending_send_buffer_high_watermark_called_(false), reset_due_to_messaging_error_(false),
      defer_processing_backedup_streams_(
          Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
  parent_.stats_.streams_active_.inc();
  if (buffer_limit > 0) {
    setWriteBufferWatermarks(buffer_limit);
  }
  stream_manager_.defer_processing_segment_size_ = parent.connection_.bufferLimit();
}

void ConnectionImpl::StreamImpl::destroy() {
  // Cancel any pending buffered data callback for the stream.
  process_buffered_data_callback_.reset();

  MultiplexedStreamImplBase::destroy();
  parent_.stats_.streams_active_.dec();
  parent_.stats_.pending_send_bytes_.sub(pending_send_data_->length());
}

void ConnectionImpl::ServerStreamImpl::destroy() {
  // Only the downstream stream should clear the downstream of the
  // memory account.
  // This occurs in destroy as we want to ensure the Stream does not get
  // reset called on it from the account.
  //
  // There are cases where a corresponding upstream stream dtor might
  // be called, but the downstream stream isn't going to terminate soon
  // such as StreamDecoderFilterCallbacks::recreateStream().
  if (buffer_memory_account_) {
    buffer_memory_account_->clearDownstream();
  }

  StreamImpl::destroy();
}

static void insertHeader(std::vector<nghttp2_nv>& headers, const HeaderEntry& header) {
  uint8_t flags = 0;
  if (header.key().isReference()) {
    flags |= NGHTTP2_NV_FLAG_NO_COPY_NAME;
  }
  if (header.value().isReference()) {
    flags |= NGHTTP2_NV_FLAG_NO_COPY_VALUE;
  }
  const absl::string_view header_key = header.key().getStringView();
  const absl::string_view header_value = header.value().getStringView();
  headers.push_back({removeConst<uint8_t>(header_key.data()),
                     removeConst<uint8_t>(header_value.data()), header_key.size(),
                     header_value.size(), flags});
}

void ConnectionImpl::StreamImpl::buildHeaders(std::vector<nghttp2_nv>& final_headers,
                                              const HeaderMap& headers) {
  final_headers.reserve(headers.size());
  headers.iterate([&final_headers](const HeaderEntry& header) -> HeaderMap::Iterate {
    insertHeader(final_headers, header);
    return HeaderMap::Iterate::Continue;
  });
}

http2::adapter::HeaderRep getRep(const HeaderString& str) {
  if (str.isReference()) {
    return str.getStringView();
  } else {
    return std::string(str.getStringView());
  }
}

std::vector<http2::adapter::Header>
ConnectionImpl::StreamImpl::buildHeaders(const HeaderMap& headers) {
  std::vector<http2::adapter::Header> out;
  out.reserve(headers.size());
  headers.iterate([&out](const HeaderEntry& header) -> HeaderMap::Iterate {
    out.push_back({getRep(header.key()), getRep(header.value())});
    return HeaderMap::Iterate::Continue;
  });
  return out;
}

void ConnectionImpl::ServerStreamImpl::encode1xxHeaders(const ResponseHeaderMap& headers) {
  ASSERT(HeaderUtility::isSpecial1xx(headers));
  encodeHeaders(headers, false);
}

void ConnectionImpl::StreamImpl::encodeHeadersBase(const HeaderMap& headers, bool end_stream) {
  nghttp2_data_provider provider;
  if (!end_stream) {
    provider.source.ptr = this;
    provider.read_callback = [](nghttp2_session*, int32_t, uint8_t*, size_t length,
                                uint32_t* data_flags, nghttp2_data_source* source,
                                void*) -> ssize_t {
      return static_cast<StreamImpl*>(source->ptr)->onDataSourceRead(length, data_flags);
    };
  }

  local_end_stream_ = end_stream;
  submitHeaders(headers, end_stream ? nullptr : &provider);
  if (parent_.sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

Status ConnectionImpl::ClientStreamImpl::encodeHeaders(const RequestHeaderMap& headers,
                                                       bool end_stream) {
  parent_.updateActiveStreamsOnEncode(*this);
  // Required headers must be present. This can only happen by some erroneous processing after the
  // downstream codecs decode.
  RETURN_IF_ERROR(HeaderUtility::checkRequiredRequestHeaders(headers));
  // This must exist outside of the scope of isUpgrade as the underlying memory is
  // needed until encodeHeadersBase has been called.
  Http::RequestHeaderMapPtr modified_headers;
  if (Http::Utility::isUpgrade(headers)) {
    modified_headers = createHeaderMap<RequestHeaderMapImpl>(headers);
    upgrade_type_ = std::string(headers.getUpgradeValue());
    Http::Utility::transformUpgradeRequestFromH1toH2(*modified_headers);
    encodeHeadersBase(*modified_headers, end_stream);
  } else if (headers.Method() && headers.Method()->value() == "CONNECT") {
    // If this is not an upgrade style connect (above branch) it is a bytestream
    // connect and should have :path and :protocol set accordingly
    // As HTTP/1.1 does not require a path for CONNECT, we may have to add one
    // if shifting codecs. For now, default to "/" - this can be made
    // configurable if necessary.
    // https://tools.ietf.org/html/draft-kinnear-httpbis-http2-transport-02
    modified_headers = createHeaderMap<RequestHeaderMapImpl>(headers);
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.use_rfc_connect")) {
      modified_headers->removeScheme();
      modified_headers->removePath();
      modified_headers->removeProtocol();
    } else {
      modified_headers->setProtocol(Headers::get().ProtocolValues.Bytestream);
      if (!headers.Path()) {
        modified_headers->setPath("/");
      }
    }
    encodeHeadersBase(*modified_headers, end_stream);
  } else {
    encodeHeadersBase(headers, end_stream);
  }
  return okStatus();
}

void ConnectionImpl::ServerStreamImpl::encodeHeaders(const ResponseHeaderMap& headers,
                                                     bool end_stream) {
  parent_.updateActiveStreamsOnEncode(*this);
  // The contract is that client codecs must ensure that :status is present.
  ASSERT(headers.Status() != nullptr);

  // This must exist outside of the scope of isUpgrade as the underlying memory is
  // needed until encodeHeadersBase has been called.
  Http::ResponseHeaderMapPtr modified_headers;
  if (Http::Utility::isUpgrade(headers)) {
    modified_headers = createHeaderMap<ResponseHeaderMapImpl>(headers);
    Http::Utility::transformUpgradeResponseFromH1toH2(*modified_headers);
    encodeHeadersBase(*modified_headers, end_stream);
  } else {
    encodeHeadersBase(headers, end_stream);
  }
}

void ConnectionImpl::StreamImpl::encodeTrailersBase(const HeaderMap& trailers) {
  parent_.updateActiveStreamsOnEncode(*this);
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;
  if (pending_send_data_->length() > 0) {
    // In this case we want trailers to come after we release all pending body data that is
    // waiting on window updates. We need to save the trailers so that we can emit them later.
    // However, for empty trailers, we don't need to to save the trailers.
    ASSERT(!pending_trailers_to_encode_);
    const bool skip_encoding_empty_trailers = trailers.empty();
    if (!skip_encoding_empty_trailers) {
      pending_trailers_to_encode_ = cloneTrailers(trailers);
      onLocalEndStream();
    }
  } else {
    submitTrailers(trailers);
    if (parent_.sendPendingFramesAndHandleError()) {
      // Intended to check through coverage that this error case is tested
      return;
    }
  }
}

void ConnectionImpl::StreamImpl::encodeMetadata(const MetadataMapVector& metadata_map_vector) {
  parent_.updateActiveStreamsOnEncode(*this);
  ASSERT(parent_.allow_metadata_);
  NewMetadataEncoder& metadata_encoder = getMetadataEncoder();
  auto sources_vec = metadata_encoder.createSources(metadata_map_vector);
  for (auto& source : sources_vec) {
    parent_.adapter_->SubmitMetadata(stream_id_, 16 * 1024, std::move(source));
  }

  if (parent_.sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

void ConnectionImpl::StreamImpl::processBufferedData() {
  ENVOY_CONN_LOG(debug, "Stream {} processing buffered data.", parent_.connection_, stream_id_);

  // Restore crash dump context when processing buffered data.
  Event::Dispatcher& dispatcher = parent_.connection_.dispatcher();
  // This method is only called from a callback placed directly on the
  // dispatcher, as such the dispatcher shouldn't have any tracked objects.
  ASSERT(dispatcher.trackedObjectStackIsEmpty());
  Envoy::ScopeTrackedObjectStack stack;
  stack.add(parent_.connection_);

  absl::Cleanup clear_current_stream_id = [this]() { parent_.current_stream_id_.reset(); };
  // TODO(kbaichoo): When we add support to *ConnectionImpl::getStream* for
  // deferred closed streams we can use their stream id here.
  if (!stream_manager_.buffered_on_stream_close_) {
    ASSERT(!parent_.current_stream_id_.has_value());
    parent_.current_stream_id_ = stream_id_;
  }

  stack.add(parent_);
  ScopeTrackerScopeState scope{&stack, dispatcher};

  if (stream_manager_.body_buffered_ && continueProcessingBufferedData()) {
    decodeData();
  }

  if (stream_manager_.trailers_buffered_ && !stream_manager_.body_buffered_ &&
      continueProcessingBufferedData()) {
    decodeTrailers();
    ASSERT(!stream_manager_.trailers_buffered_);
  }

  // Reset cases are handled by resetStream and directly invoke onStreamClose,
  // which consumes the buffered_on_stream_close_ so we don't invoke
  // onStreamClose twice.
  if (stream_manager_.buffered_on_stream_close_ && !stream_manager_.hasBufferedBodyOrTrailers()) {
    ASSERT(!reset_reason_.has_value());
    ENVOY_CONN_LOG(debug, "invoking onStreamClose for stream: {} via processBufferedData",
                   parent_.connection_, stream_id_);
    // We only buffer the onStreamClose if we had no errors.
    if (Status status = parent_.onStreamClose(this, 0); !status.ok()) {
      ENVOY_CONN_LOG(debug, "error invoking onStreamClose: {}", parent_.connection_,
                     status.message());
    }
  }
}

void ConnectionImpl::StreamImpl::grantPeerAdditionalStreamWindow() {
  parent_.adapter_->MarkDataConsumedForStream(stream_id_, unconsumed_bytes_);
  unconsumed_bytes_ = 0;
  if (parent_.sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

void ConnectionImpl::StreamImpl::readDisable(bool disable) {
  ENVOY_CONN_LOG(debug, "Stream {} {}, unconsumed_bytes {} read_disable_count {}",
                 parent_.connection_, stream_id_, (disable ? "disabled" : "enabled"),
                 unconsumed_bytes_, read_disable_count_);
  if (disable) {
    ++read_disable_count_;
  } else {
    ASSERT(read_disable_count_ > 0);
    --read_disable_count_;
    if (!buffersOverrun()) {
      scheduleProcessingOfBufferedData(false);
      if (shouldAllowPeerAdditionalStreamWindow()) {
        grantPeerAdditionalStreamWindow();
      }
    }
  }
}

void ConnectionImpl::StreamImpl::scheduleProcessingOfBufferedData(bool schedule_next_iteration) {
  if (defer_processing_backedup_streams_ && stream_manager_.hasBufferedBodyOrTrailers()) {
    if (!process_buffered_data_callback_) {
      process_buffered_data_callback_ = parent_.connection_.dispatcher().createSchedulableCallback(
          [this]() { processBufferedData(); });
    }

    // We schedule processing to occur in another callback to avoid
    // reentrant and deep call stacks.
    if (schedule_next_iteration) {
      process_buffered_data_callback_->scheduleCallbackNextIteration();
    } else {
      process_buffered_data_callback_->scheduleCallbackCurrentIteration();
    }
  }
}

void ConnectionImpl::StreamImpl::pendingRecvBufferHighWatermark() {
  // If `defer_processing_backedup_streams_`, read disabling here can become
  // dangerous as it can prevent us from processing buffered data.
  if (!defer_processing_backedup_streams_) {
    ENVOY_CONN_LOG(debug, "recv buffer over limit ", parent_.connection_);
    ASSERT(!pending_receive_buffer_high_watermark_called_);
    pending_receive_buffer_high_watermark_called_ = true;
    readDisable(true);
  }
}

void ConnectionImpl::StreamImpl::pendingRecvBufferLowWatermark() {
  // If `defer_processing_backedup_streams_`, we don't read disable on
  // high watermark, so we shouldn't read disable here.
  if (defer_processing_backedup_streams_) {
    if (shouldAllowPeerAdditionalStreamWindow()) {
      // We should grant additional stream window here, in case the
      // `pending_recv_buffer_` was blocking flow control updates
      // from going to the peer.
      grantPeerAdditionalStreamWindow();
    }
  } else {
    ENVOY_CONN_LOG(debug, "recv buffer under limit ", parent_.connection_);
    ASSERT(pending_receive_buffer_high_watermark_called_);
    pending_receive_buffer_high_watermark_called_ = false;
    readDisable(false);
  }
}

void ConnectionImpl::StreamImpl::decodeData() {
  if (defer_processing_backedup_streams_ && buffersOverrun()) {
    ENVOY_CONN_LOG(trace, "Stream {} buffering decodeData() call.", parent_.connection_,
                   stream_id_);
    stream_manager_.body_buffered_ = true;
    return;
  }

  // Some buffered body will be consumed. If there remains buffered body after
  // this call, set this to true.
  stream_manager_.body_buffered_ = false;

  bool already_drained_data = false;
  // It's possible that we are waiting to send a deferred reset, so only raise data if local
  // is not complete.
  if (!deferred_reset_) {
    // We should decode data in chunks only if we have defer processing enabled
    // with a non-zero defer_processing_segment_size, and the buffer holds more
    // data than the defer_processing_segment_size. Otherwise, push the
    // entire buffer through.
    const bool decode_data_in_chunk =
        defer_processing_backedup_streams_ && stream_manager_.decodeAsChunks() &&
        pending_recv_data_->length() > stream_manager_.defer_processing_segment_size_;

    if (decode_data_in_chunk) {
      Buffer::OwnedImpl chunk_buffer;
      // TODO(kbaichoo): Consider implementing an approximate move for chunking.
      chunk_buffer.move(*pending_recv_data_, stream_manager_.defer_processing_segment_size_);

      // With the current implementation this should always be true,
      // though this can change with approximation.
      stream_manager_.body_buffered_ = true;
      ASSERT(pending_recv_data_->length() > 0);

      decoder().decodeData(chunk_buffer, sendEndStream());
      already_drained_data = true;

      if (!buffersOverrun()) {
        scheduleProcessingOfBufferedData(true);
      }
    } else {
      // Send the entire buffer through.
      decoder().decodeData(*pending_recv_data_, sendEndStream());
    }
  }

  if (!already_drained_data) {
    pending_recv_data_->drain(pending_recv_data_->length());
  }
}

void ConnectionImpl::ClientStreamImpl::decodeHeaders() {
  auto& headers = absl::get<ResponseHeaderMapPtr>(headers_or_trailers_);
  const uint64_t status = Http::Utility::getResponseStatus(*headers);

  if (!upgrade_type_.empty() && headers->Status()) {
    Http::Utility::transformUpgradeResponseFromH2toH1(*headers, upgrade_type_);
  }

  // Non-informational headers are non-1xx OR 101-SwitchingProtocols, since 101 implies that further
  // proxying is on an upgrade path.
  received_noninformational_headers_ =
      !CodeUtility::is1xx(status) || status == enumToInt(Http::Code::SwitchingProtocols);

  if (HeaderUtility::isSpecial1xx(*headers)) {
    ASSERT(!remote_end_stream_);
    response_decoder_.decode1xxHeaders(std::move(headers));
  } else {
    response_decoder_.decodeHeaders(std::move(headers), sendEndStream());
  }
}

bool ConnectionImpl::StreamImpl::maybeDeferDecodeTrailers() {
  ASSERT(!deferred_reset_.has_value());
  // Buffer trailers if we're deferring processing and not flushing all data
  // through and either
  // 1) Buffers are overrun
  // 2) There's buffered body which should get processed before these trailers
  //    to avoid losing data.
  if (defer_processing_backedup_streams_ && (buffersOverrun() || stream_manager_.body_buffered_)) {
    stream_manager_.trailers_buffered_ = true;
    ENVOY_CONN_LOG(trace, "Stream {} buffering decodeTrailers() call.", parent_.connection_,
                   stream_id_);
    return true;
  }

  return false;
}

void ConnectionImpl::ClientStreamImpl::decodeTrailers() {
  if (maybeDeferDecodeTrailers()) {
    return;
  }

  // Consume any buffered trailers.
  stream_manager_.trailers_buffered_ = false;

  response_decoder_.decodeTrailers(
      std::move(absl::get<ResponseTrailerMapPtr>(headers_or_trailers_)));
}

void ConnectionImpl::ServerStreamImpl::decodeHeaders() {
  auto& headers = absl::get<RequestHeaderMapPtr>(headers_or_trailers_);
  if (Http::Utility::isH2UpgradeRequest(*headers)) {
    Http::Utility::transformUpgradeRequestFromH2toH1(*headers);
  }
  request_decoder_->decodeHeaders(std::move(headers), sendEndStream());
}

void ConnectionImpl::ServerStreamImpl::decodeTrailers() {
  if (maybeDeferDecodeTrailers()) {
    return;
  }

  // Consume any buffered trailers.
  stream_manager_.trailers_buffered_ = false;

  request_decoder_->decodeTrailers(
      std::move(absl::get<RequestTrailerMapPtr>(headers_or_trailers_)));
}

void ConnectionImpl::StreamImpl::pendingSendBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "send buffer over limit ", parent_.connection_);
  ASSERT(!pending_send_buffer_high_watermark_called_);
  pending_send_buffer_high_watermark_called_ = true;
  runHighWatermarkCallbacks();
}

void ConnectionImpl::StreamImpl::pendingSendBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "send buffer under limit ", parent_.connection_);
  ASSERT(pending_send_buffer_high_watermark_called_);
  pending_send_buffer_high_watermark_called_ = false;
  runLowWatermarkCallbacks();
}

void ConnectionImpl::StreamImpl::saveHeader(HeaderString&& name, HeaderString&& value) {
  if (!Utility::reconstituteCrumbledCookies(name, value, cookies_)) {
    headers().addViaMove(std::move(name), std::move(value));
  }
}

void ConnectionImpl::StreamImpl::submitTrailers(const HeaderMap& trailers) {
  ASSERT(local_end_stream_);
  const bool skip_encoding_empty_trailers = trailers.empty();
  if (skip_encoding_empty_trailers) {
    ENVOY_CONN_LOG(debug, "skipping submitting trailers", parent_.connection_);

    // Instead of submitting empty trailers, we send empty data instead.
    Buffer::OwnedImpl empty_buffer;
    encodeDataHelper(empty_buffer, /*end_stream=*/true, skip_encoding_empty_trailers);
    return;
  }

  std::vector<http2::adapter::Header> final_headers = buildHeaders(trailers);
  parent_.adapter_->SubmitTrailer(stream_id_, final_headers);
}

ssize_t ConnectionImpl::StreamImpl::onDataSourceRead(uint64_t length, uint32_t* data_flags) {
  if (pending_send_data_->length() == 0 && !local_end_stream_) {
    ASSERT(!data_deferred_);
    data_deferred_ = true;
    return NGHTTP2_ERR_DEFERRED;
  } else {
    *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
    if (local_end_stream_ && pending_send_data_->length() <= length) {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
      if (pending_trailers_to_encode_) {
        // We need to tell the library to not set end stream so that we can emit the trailers.
        *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
        submitTrailers(*pending_trailers_to_encode_);
        pending_trailers_to_encode_.reset();
      }
    }

    return std::min(length, pending_send_data_->length());
  }
}

void ConnectionImpl::StreamImpl::onDataSourceSend(const uint8_t* framehd, size_t length) {
  // In this callback we are writing out a raw DATA frame without copying. nghttp2 assumes that we
  // "just know" that the frame header is 9 bytes.
  // https://nghttp2.org/documentation/types.html#c.nghttp2_send_data_callback

  parent_.protocol_constraints_.incrementOutboundDataFrameCount();

  Buffer::OwnedImpl output;
  parent_.addOutboundFrameFragment(output, framehd, H2_FRAME_HEADER_SIZE);
  if (!parent_.protocol_constraints_.checkOutboundFrameLimits().ok()) {
    ENVOY_CONN_LOG(debug, "error sending data frame: Too many frames in the outbound queue",
                   parent_.connection_);
    setDetails(Http2ResponseCodeDetails::get().outbound_frame_flood);
  }

  parent_.stats_.pending_send_bytes_.sub(length);
  output.move(*pending_send_data_, length);
  parent_.connection_.write(output, false);
}

void ConnectionImpl::ClientStreamImpl::submitHeaders(const HeaderMap& headers,
                                                     nghttp2_data_provider* provider) {
  ASSERT(stream_id_ == -1);
  // TODO(birenroy): Once using the new wrapper, migrate callers from nghttp2_data_provider to
  // DataFrameSource.
  std::unique_ptr<http2::adapter::DataFrameSource> data_frame_source;
  if (provider) {
    data_frame_source = http2::adapter::MakeZeroCopyDataFrameSource(
        *provider, &parent_.connection_,
        [](nghttp2_session*, nghttp2_frame* frame, const uint8_t* framehd, size_t length,
           nghttp2_data_source* source, void*) -> int {
          ASSERT(frame->data.padlen == 0);
          static_cast<StreamImpl*>(source->ptr)->onDataSourceSend(framehd, length);
          return 0;
        });
  }
  stream_id_ =
      parent_.adapter_->SubmitRequest(buildHeaders(headers), std::move(data_frame_source), base());
  ASSERT(stream_id_ > 0);
}

void ConnectionImpl::ServerStreamImpl::submitHeaders(const HeaderMap& headers,
                                                     nghttp2_data_provider* provider) {
  ASSERT(stream_id_ != -1);
  std::unique_ptr<http2::adapter::DataFrameSource> data_frame_source;
  if (provider) {
    data_frame_source = http2::adapter::MakeZeroCopyDataFrameSource(
        *provider, &parent_.connection_,
        [](nghttp2_session*, nghttp2_frame* frame, const uint8_t* framehd, size_t length,
           nghttp2_data_source* source, void*) -> int {
          ASSERT(frame->data.padlen == 0);
          static_cast<StreamImpl*>(source->ptr)->onDataSourceSend(framehd, length);
          return 0;
        });
  }
  parent_.adapter_->SubmitResponse(stream_id_, buildHeaders(headers), std::move(data_frame_source));
}

void ConnectionImpl::StreamImpl::onPendingFlushTimer() {
  ENVOY_CONN_LOG(debug, "pending stream flush timeout", parent_.connection_);
  MultiplexedStreamImplBase::onPendingFlushTimer();
  parent_.stats_.tx_flush_timeout_.inc();
  ASSERT(local_end_stream_ && !local_end_stream_sent_);
  // This will emit a reset frame for this stream and close the stream locally. No reset callbacks
  // will be run because higher layers think the stream is already finished.
  resetStreamWorker(StreamResetReason::LocalReset);
  if (parent_.sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

void ConnectionImpl::StreamImpl::encodeData(Buffer::Instance& data, bool end_stream) {
  parent_.updateActiveStreamsOnEncode(*this);
  ASSERT(!local_end_stream_);
  encodeDataHelper(data, end_stream,
                   /*skip_encoding_empty_trailers=*/
                   false);
}

void ConnectionImpl::StreamImpl::encodeDataHelper(Buffer::Instance& data, bool end_stream,
                                                  bool skip_encoding_empty_trailers) {
  if (skip_encoding_empty_trailers) {
    ASSERT(data.length() == 0 && end_stream);
  }

  local_end_stream_ = end_stream;
  parent_.stats_.pending_send_bytes_.add(data.length());
  pending_send_data_->move(data);
  if (data_deferred_) {
    bool success = parent_.adapter_->ResumeStream(stream_id_);
    ASSERT(success);

    data_deferred_ = false;
  }

  if (parent_.sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
  if (local_end_stream_) {
    onLocalEndStream();
  }
}

void ConnectionImpl::ServerStreamImpl::resetStream(StreamResetReason reason) {
  // Clear the downstream on the account since we're resetting the downstream.
  if (buffer_memory_account_) {
    buffer_memory_account_->clearDownstream();
  }

  StreamImpl::resetStream(reason);
}

void ConnectionImpl::StreamImpl::resetStream(StreamResetReason reason) {
  reset_reason_ = reason;

  // Higher layers expect calling resetStream() to immediately raise reset callbacks.
  runResetCallbacks(reason);

  // If we've bufferedOnStreamClose for this stream, we shouldn't propagate this
  // reset as nghttp2 will have forgotten about the stream.
  if (stream_manager_.buffered_on_stream_close_) {
    ENVOY_CONN_LOG(
        trace, "Stopped propagating reset to nghttp2 as we've buffered onStreamClose for stream {}",
        parent_.connection_, stream_id_);
    // The stream didn't originally have an NGHTTP2 error, since we buffered
    // its stream close.
    if (Status status = parent_.onStreamClose(this, 0); !status.ok()) {
      ENVOY_CONN_LOG(debug, "error invoking onStreamClose: {}", parent_.connection_,
                     status.message());
    }
    return;
  }

  // If we submit a reset, nghttp2 will cancel outbound frames that have not yet been sent.
  // We want these frames to go out so we defer the reset until we send all of the frames that
  // end the local stream. However, if we're resetting the stream due to
  // overload, we should reset the stream as soon as possible to free used
  // resources.
  if (useDeferredReset() && local_end_stream_ && !local_end_stream_sent_ &&
      reason != StreamResetReason::OverloadManager) {
    ASSERT(parent_.getStream(stream_id_) != nullptr);
    parent_.pending_deferred_reset_streams_.emplace(stream_id_, this);
    deferred_reset_ = reason;
    ENVOY_CONN_LOG(trace, "deferred reset stream", parent_.connection_);
  } else {
    resetStreamWorker(reason);
  }

  // We must still call sendPendingFrames() in both the deferred and not deferred path. This forces
  // the cleanup logic to run which will reset the stream in all cases if all data frames could not
  // be sent.
  if (parent_.sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

void ConnectionImpl::StreamImpl::resetStreamWorker(StreamResetReason reason) {
  if (stream_id_ == -1) {
    // Handle the case where client streams are reset before headers are created.
    return;
  }
  parent_.adapter_->SubmitRst(stream_id_,
                              static_cast<http2::adapter::Http2ErrorCode>(reasonToReset(reason)));
}

MetadataEncoder& ConnectionImpl::StreamImpl::getMetadataEncoderOld() {
  if (metadata_encoder_old_ == nullptr) {
    metadata_encoder_old_ = std::make_unique<MetadataEncoder>();
  }
  return *metadata_encoder_old_;
}

NewMetadataEncoder& ConnectionImpl::StreamImpl::getMetadataEncoder() {
  if (metadata_encoder_ == nullptr) {
    metadata_encoder_ = std::make_unique<NewMetadataEncoder>();
  }
  return *metadata_encoder_;
}

MetadataDecoder& ConnectionImpl::StreamImpl::getMetadataDecoder() {
  if (metadata_decoder_ == nullptr) {
    auto cb = [this](MetadataMapPtr&& metadata_map_ptr) {
      this->onMetadataDecoded(std::move(metadata_map_ptr));
    };
    metadata_decoder_ = std::make_unique<MetadataDecoder>(cb);
  }
  return *metadata_decoder_;
}

void ConnectionImpl::StreamImpl::onMetadataDecoded(MetadataMapPtr&& metadata_map_ptr) {
  // Empty metadata maps should not be decoded.
  if (metadata_map_ptr->empty()) {
    ENVOY_CONN_LOG(debug, "decode metadata called with empty map, skipping", parent_.connection_);
    parent_.stats_.metadata_empty_frames_.inc();
  } else {
    decoder().decodeMetadata(std::move(metadata_map_ptr));
  }
}

void ConnectionImpl::StreamImpl::setAccount(Buffer::BufferMemoryAccountSharedPtr account) {
  buffer_memory_account_ = account;
  pending_recv_data_->bindAccount(buffer_memory_account_);
  pending_send_data_->bindAccount(buffer_memory_account_);
}

ConnectionImpl::ConnectionImpl(Network::Connection& connection, CodecStats& stats,
                               Random::RandomGenerator& random_generator,
                               const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
                               const uint32_t max_headers_kb, const uint32_t max_headers_count)
    : use_oghttp2_library_(
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http2_use_oghttp2")),
      stats_(stats), connection_(connection), max_headers_kb_(max_headers_kb),
      max_headers_count_(max_headers_count),
      per_stream_buffer_limit_(http2_options.initial_stream_window_size().value()),
      stream_error_on_invalid_http_messaging_(
          http2_options.override_stream_error_on_invalid_http_message().value()),
      protocol_constraints_(stats, http2_options), dispatching_(false), raised_goaway_(false),
      delay_keepalive_timeout_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.http2_delay_keepalive_timeout")),
      random_(random_generator),
      last_received_data_time_(connection_.dispatcher().timeSource().monotonicTime()) {
  if (http2_options.has_connection_keepalive()) {
    keepalive_interval_ = std::chrono::milliseconds(
        PROTOBUF_GET_MS_OR_DEFAULT(http2_options.connection_keepalive(), interval, 0));
    keepalive_timeout_ = std::chrono::milliseconds(
        PROTOBUF_GET_MS_REQUIRED(http2_options.connection_keepalive(), timeout));
    keepalive_interval_jitter_percent_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
        http2_options.connection_keepalive(), interval_jitter, 15.0);

    if (keepalive_interval_.count() > 0) {
      keepalive_send_timer_ = connection.dispatcher().createTimer([this]() { sendKeepalive(); });
    }
    keepalive_timeout_timer_ =
        connection.dispatcher().createTimer([this]() { onKeepaliveResponseTimeout(); });

    // This call schedules the initial interval, with jitter.
    onKeepaliveResponse();
  }
}

ConnectionImpl::~ConnectionImpl() {
  for (const auto& stream : active_streams_) {
    stream->destroy();
  }
}

void ConnectionImpl::sendKeepalive() {
  ASSERT(keepalive_timeout_timer_);
  if (keepalive_timeout_timer_->enabled()) {
    ENVOY_CONN_LOG(trace, "Skipping PING: already awaiting PING ACK {}", connection_);
    return;
  }

  // Include the current time as the payload to help with debugging.
  SystemTime now = connection_.dispatcher().timeSource().systemTime();
  uint64_t ms_since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  ENVOY_CONN_LOG(trace, "Sending keepalive PING {}", connection_, ms_since_epoch);

  adapter_->SubmitPing(ms_since_epoch);

  if (sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
  keepalive_timeout_timer_->enableTimer(keepalive_timeout_);
}

void ConnectionImpl::onKeepaliveResponse() {
  // Check the timers for nullptr in case the peer sent an unsolicited PING ACK.
  if (keepalive_timeout_timer_ != nullptr) {
    keepalive_timeout_timer_->disableTimer();
  }
  if (keepalive_send_timer_ != nullptr && keepalive_interval_.count()) {
    uint64_t interval_ms = keepalive_interval_.count();
    const uint64_t jitter_percent_mod = keepalive_interval_jitter_percent_ * interval_ms / 100;
    if (jitter_percent_mod > 0) {
      interval_ms += random_.random() % jitter_percent_mod;
    }
    keepalive_send_timer_->enableTimer(std::chrono::milliseconds(interval_ms));
  }
}

void ConnectionImpl::onKeepaliveResponseTimeout() {
  ENVOY_CONN_LOG_EVENT(debug, "h2_ping_timeout", "Closing connection due to keepalive timeout",
                       connection_);
  stats_.keepalive_timeout_.inc();
  connection_.close(Network::ConnectionCloseType::NoFlush);
}

bool ConnectionImpl::slowContainsStreamId(int32_t stream_id) const {
  for (const auto& stream : active_streams_) {
    if (stream->stream_id_ == stream_id) {
      return true;
    }
  }

  return false;
}

Http::Status ConnectionImpl::dispatch(Buffer::Instance& data) {
  ScopeTrackerScopeState scope(this, connection_.dispatcher());
  ENVOY_CONN_LOG(trace, "dispatching {} bytes", connection_, data.length());
  // Make sure that dispatching_ is set to false after dispatching, even when
  // ConnectionImpl::dispatch returns early or throws an exception (consider removing if there is a
  // single return after exception removal (#10878)).
  Cleanup cleanup([this]() {
    dispatching_ = false;
    current_slice_ = nullptr;
    current_stream_id_.reset();
  });
  last_received_data_time_ = connection_.dispatcher().timeSource().monotonicTime();
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    current_slice_ = &slice;
    dispatching_ = true;
    ssize_t rc;
    rc = adapter_->ProcessBytes(absl::string_view(static_cast<char*>(slice.mem_), slice.len_));
    if (!nghttp2_callback_status_.ok()) {
      return nghttp2_callback_status_;
    }
    // This error is returned when nghttp2 library detected a frame flood by one of its
    // internal mechanisms. Most flood protection is done by Envoy's codec and this error
    // should never be returned. However it is handled here in case nghttp2 has some flood
    // protections that Envoy's codec does not have.
    if (rc == NGHTTP2_ERR_FLOODED) {
      return bufferFloodError(
          "Flooding was detected in this HTTP/2 session, and it must be closed");
    }
    if (rc != static_cast<ssize_t>(slice.len_)) {
      return codecProtocolError(nghttp2_strerror(rc));
    }

    current_slice_ = nullptr;
    dispatching_ = false;
    current_stream_id_.reset();
  }

  ENVOY_CONN_LOG(trace, "dispatched {} bytes", connection_, data.length());
  data.drain(data.length());

  // Decoding incoming frames can generate outbound frames so flush pending.
  return sendPendingFrames();
}

const ConnectionImpl::StreamImpl* ConnectionImpl::getStream(int32_t stream_id) const {
  // Delegate to the non-const version.
  return const_cast<ConnectionImpl*>(this)->getStream(stream_id);
}

ConnectionImpl::StreamImpl* ConnectionImpl::getStreamUnchecked(int32_t stream_id) {
  return static_cast<StreamImpl*>(adapter_->GetStreamUserData(stream_id));
}

ConnectionImpl::StreamImpl* ConnectionImpl::getStream(int32_t stream_id) {
  StreamImpl* stream = getStreamUnchecked(stream_id);
  SLOW_ASSERT(stream != nullptr || !slowContainsStreamId(stream_id));
  return stream;
}

int ConnectionImpl::onData(int32_t stream_id, const uint8_t* data, size_t len) {
  ASSERT(connection_.state() == Network::Connection::State::Open);
  StreamImpl* stream = getStream(stream_id);
  // If this results in buffering too much data, the watermark buffer will call
  // pendingRecvBufferHighWatermark, resulting in ++read_disable_count_
  stream->pending_recv_data_->add(data, len);
  // Update the window to the peer unless some consumer of this stream's data has hit a flow control
  // limit and disabled reads on this stream
  if (stream->shouldAllowPeerAdditionalStreamWindow()) {
    adapter_->MarkDataConsumedForStream(stream_id, len);
  } else {
    stream->unconsumed_bytes_ += len;
  }
  return 0;
}

void ConnectionImpl::goAway() {
  adapter_->SubmitGoAway(adapter_->GetHighestReceivedStreamId(),
                         http2::adapter::Http2ErrorCode::HTTP2_NO_ERROR, "");

  if (sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

void ConnectionImpl::shutdownNotice() {
  adapter_->SubmitShutdownNotice();

  if (sendPendingFramesAndHandleError()) {
    // Intended to check through coverage that this error case is tested
    return;
  }
}

Status ConnectionImpl::protocolErrorForTest() {
  adapter_->SubmitGoAway(adapter_->GetHighestReceivedStreamId(),
                         http2::adapter::Http2ErrorCode::PROTOCOL_ERROR, "");

  return sendPendingFrames();
}

Status ConnectionImpl::onBeforeFrameReceived(const nghttp2_frame_hd* hd) {
  ENVOY_CONN_LOG(trace, "about to recv frame type={}, flags={}, stream_id={}", connection_,
                 static_cast<uint64_t>(hd->type), static_cast<uint64_t>(hd->flags), hd->stream_id);
  ASSERT(connection_.state() == Network::Connection::State::Open);

  current_stream_id_ = hd->stream_id;
  // Track all the frames without padding here, since this is the only callback we receive
  // for some of them (e.g. CONTINUATION frame, frames sent on closed streams, etc.).
  // HEADERS frame is tracked in onBeginHeaders(), DATA frame is tracked in onFrameReceived().
  auto status = okStatus();
  if (hd->type != NGHTTP2_HEADERS && hd->type != NGHTTP2_DATA) {
    status = trackInboundFrames(hd, 0);
  }

  return status;
}

ABSL_MUST_USE_RESULT
enum GoAwayErrorCode ngHttp2ErrorCodeToErrorCode(uint32_t code) noexcept {
  switch (code) {
  case NGHTTP2_NO_ERROR:
    return GoAwayErrorCode::NoError;
  default:
    return GoAwayErrorCode::Other;
  }
}

Status ConnectionImpl::onFrameReceived(const nghttp2_frame* frame) {
  ENVOY_CONN_LOG(trace, "recv frame type={}", connection_, static_cast<uint64_t>(frame->hd.type));
  ASSERT(connection_.state() == Network::Connection::State::Open);

  // onFrameReceived() is called with a complete HEADERS frame assembled from all the HEADERS
  // and CONTINUATION frames, but we track them separately: HEADERS frames in onBeginHeaders()
  // and CONTINUATION frames in onBeforeFrameReceived().
  ASSERT(frame->hd.type != NGHTTP2_CONTINUATION);

  if ((frame->hd.type == NGHTTP2_PING) && (frame->ping.hd.flags & NGHTTP2_FLAG_ACK)) {
    // The ``opaque_data`` should be exactly what was sent in the ping, which is
    // was the current time when the ping was sent. This can be useful while debugging
    // to match the ping and ack.
    uint64_t data;
    safeMemcpy(&data, &(frame->ping.opaque_data));
    ENVOY_CONN_LOG(trace, "recv PING ACK {}", connection_, data);

    onKeepaliveResponse();
    return okStatus();
  }

  // In slow networks, HOL blocking can prevent the ping response from coming in a reasonable
  // amount of time. To avoid HOL blocking influence, if we receive *any* frame extend the
  // timeout for another timeout period. This will still timeout the connection if there is no
  // activity, but if there is frame activity we assume the connection is still healthy and the
  // PING ACK may be delayed behind other frames.
  if (delay_keepalive_timeout_ && keepalive_timeout_timer_ != nullptr &&
      keepalive_timeout_timer_->enabled()) {
    keepalive_timeout_timer_->enableTimer(keepalive_timeout_);
  }

  if (frame->hd.type == NGHTTP2_DATA) {
    RETURN_IF_ERROR(trackInboundFrames(&frame->hd, frame->data.padlen));
  }

  // Only raise GOAWAY once, since we don't currently expose stream information. Shutdown
  // notifications are the same as a normal GOAWAY.
  // TODO: handle multiple GOAWAY frames.
  if (frame->hd.type == NGHTTP2_GOAWAY && !raised_goaway_) {
    ASSERT(frame->hd.stream_id == 0);
    raised_goaway_ = true;
    callbacks().onGoAway(ngHttp2ErrorCodeToErrorCode(frame->goaway.error_code));
    return okStatus();
  }

  if (frame->hd.type == NGHTTP2_SETTINGS && frame->hd.flags == NGHTTP2_FLAG_NONE) {
    onSettings(frame->settings);
  }

  StreamImpl* stream = getStream(frame->hd.stream_id);
  if (!stream) {
    return okStatus();
  }

  // Track bytes sent and received.
  if (frame->hd.type != METADATA_FRAME_TYPE) {
    stream->bytes_meter_->addWireBytesReceived(frame->hd.length + H2_FRAME_HEADER_SIZE);
  }
  if (frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_CONTINUATION) {
    stream->bytes_meter_->addHeaderBytesReceived(frame->hd.length + H2_FRAME_HEADER_SIZE);
  }

  switch (frame->hd.type) {
  case NGHTTP2_HEADERS: {
    stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
    if (!stream->cookies_.empty()) {
      HeaderString key(Headers::get().Cookie);
      stream->headers().addViaMove(std::move(key), std::move(stream->cookies_));
    }

    switch (frame->headers.cat) {
    case NGHTTP2_HCAT_RESPONSE:
    case NGHTTP2_HCAT_REQUEST: {
      stream->decodeHeaders();
      break;
    }

    case NGHTTP2_HCAT_HEADERS: {
      // It's possible that we are waiting to send a deferred reset, so only raise headers/trailers
      // if local is not complete.
      if (!stream->deferred_reset_) {
        if (adapter_->IsServerSession() || stream->received_noninformational_headers_) {
          ASSERT(stream->remote_end_stream_);
          stream->decodeTrailers();
        } else {
          // We're a client session and still waiting for non-informational headers.
          stream->decodeHeaders();
        }
      }
      break;
    }

    default:
      // We do not currently support push.
      ENVOY_BUG(false, "push not supported");
    }

    break;
  }
  case NGHTTP2_DATA: {
    stream->remote_end_stream_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
    stream->decodeData();
    break;
  }
  case NGHTTP2_RST_STREAM: {
    ENVOY_CONN_LOG(trace, "remote reset: {}", connection_, frame->rst_stream.error_code);
    stats_.rx_reset_.inc();
    break;
  }
  }

  return okStatus();
}

int ConnectionImpl::onFrameSend(const nghttp2_frame* frame) {
  // The nghttp2 library does not cleanly give us a way to determine whether we received invalid
  // data from our peer. Sometimes it raises the invalid frame callback, and sometimes it does not.
  // In all cases however it will attempt to send a GOAWAY frame with an error status. If we see
  // an outgoing frame of this type, we will return an error code so that we can abort execution.
  ENVOY_CONN_LOG(trace, "sent frame type={}, stream_id={}, length={}", connection_,
                 static_cast<uint64_t>(frame->hd.type), frame->hd.stream_id, frame->hd.length);
  StreamImpl* stream = getStream(frame->hd.stream_id);
  if (stream != nullptr) {
    if (frame->hd.type != METADATA_FRAME_TYPE) {
      stream->bytes_meter_->addWireBytesSent(frame->hd.length + H2_FRAME_HEADER_SIZE);
    }
    if (frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_CONTINUATION) {
      stream->bytes_meter_->addHeaderBytesSent(frame->hd.length + H2_FRAME_HEADER_SIZE);
    }
  }
  switch (frame->hd.type) {
  case NGHTTP2_GOAWAY: {
    ENVOY_CONN_LOG(debug, "sent goaway code={}", connection_, frame->goaway.error_code);
    if (frame->goaway.error_code != NGHTTP2_NO_ERROR) {
      // TODO(mattklein123): Returning this error code abandons standard nghttp2 frame accounting.
      // As such, it is not reliable to call sendPendingFrames() again after this and we assume
      // that the connection is going to get torn down immediately. One byproduct of this is that
      // we need to cancel all pending flush stream timeouts since they can race with connection
      // teardown. As part of the work to remove exceptions we should aim to clean up all of this
      // error handling logic and only handle this type of case at the end of dispatch.
      for (auto& stream : active_streams_) {
        stream->disarmStreamIdleTimer();
      }
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    break;
  }

  case NGHTTP2_RST_STREAM: {
    ENVOY_CONN_LOG(debug, "sent reset code={}", connection_, frame->rst_stream.error_code);
    stats_.tx_reset_.inc();
    break;
  }

  case NGHTTP2_HEADERS:
  case NGHTTP2_DATA: {
    StreamImpl* stream = getStream(frame->hd.stream_id);
    stream->local_end_stream_sent_ = frame->hd.flags & NGHTTP2_FLAG_END_STREAM;
    break;
  }
  }

  return 0;
}

int ConnectionImpl::onError(absl::string_view error) {
  ENVOY_CONN_LOG(debug, "invalid http2: {}", connection_, error);
  return 0;
}

int ConnectionImpl::onInvalidFrame(int32_t stream_id, int error_code) {
  ENVOY_CONN_LOG(debug, "invalid frame: {} on stream {}", connection_, nghttp2_strerror(error_code),
                 stream_id);

  // Set details of error_code in the stream whenever we have one.
  StreamImpl* stream = getStreamUnchecked(stream_id);
  if (stream != nullptr) {
    stream->setDetails(Http2ResponseCodeDetails::get().errorDetails(error_code));
  }

  switch (error_code) {
  case NGHTTP2_ERR_REFUSED_STREAM:

    stats_.stream_refused_errors_.inc();
    return 0;

  case NGHTTP2_ERR_HTTP_HEADER:
  case NGHTTP2_ERR_HTTP_MESSAGING:
    stats_.rx_messaging_error_.inc();
    if (stream_error_on_invalid_http_messaging_) {
      // The stream is about to be closed due to an invalid header or messaging. Don't kill the
      // entire connection if one stream has bad headers or messaging.
      if (stream != nullptr) {
        // See comment below in onStreamClose() for why we do this.
        stream->reset_due_to_messaging_error_ = true;
      }
      return 0;
    }
    break;

  case NGHTTP2_ERR_FLOW_CONTROL:
  case NGHTTP2_ERR_PROTO:
  case NGHTTP2_ERR_STREAM_CLOSED:
    // Known error conditions that should trigger connection close.
    break;

  default:
    // Unknown error conditions. Trigger ENVOY_BUG and connection close.
    ENVOY_BUG(false, absl::StrCat("Unexpected error_code: ", error_code));
    break;
  }

  // Cause dispatch to return with an error code.
  return NGHTTP2_ERR_CALLBACK_FAILURE;
}

int ConnectionImpl::onBeforeFrameSend(const nghttp2_frame* frame) {
  ENVOY_CONN_LOG(trace, "about to send frame type={}, flags={}", connection_,
                 static_cast<uint64_t>(frame->hd.type), static_cast<uint64_t>(frame->hd.flags));
  ASSERT(!is_outbound_flood_monitored_control_frame_);
  // Flag flood monitored outbound control frames.
  is_outbound_flood_monitored_control_frame_ =
      ((frame->hd.type == NGHTTP2_PING || frame->hd.type == NGHTTP2_SETTINGS) &&
       frame->hd.flags & NGHTTP2_FLAG_ACK) ||
      frame->hd.type == NGHTTP2_RST_STREAM;
  return 0;
}

void ConnectionImpl::addOutboundFrameFragment(Buffer::OwnedImpl& output, const uint8_t* data,
                                              size_t length) {
  // Reset the outbound frame type (set in the onBeforeFrameSend callback) since the
  // onBeforeFrameSend callback is not called for DATA frames.
  bool is_outbound_flood_monitored_control_frame = false;
  std::swap(is_outbound_flood_monitored_control_frame, is_outbound_flood_monitored_control_frame_);
  auto releasor =
      protocol_constraints_.incrementOutboundFrameCount(is_outbound_flood_monitored_control_frame);
  output.add(data, length);
  output.addDrainTracker(releasor);
}

ssize_t ConnectionImpl::onSend(const uint8_t* data, size_t length) {
  ENVOY_CONN_LOG(trace, "send data: bytes={}", connection_, length);
  Buffer::OwnedImpl buffer;
  addOutboundFrameFragment(buffer, data, length);

  // While the buffer is transient the fragment it contains will be moved into the
  // write_buffer_ of the underlying connection_ by the write method below.
  // This creates lifetime dependency between the write_buffer_ of the underlying connection
  // and the codec object. Specifically the write_buffer_ MUST be either fully drained or
  // deleted before the codec object is deleted. This is presently guaranteed by the
  // destruction order of the Network::ConnectionImpl object where write_buffer_ is
  // destroyed before the filter_manager_ which owns the codec through Http::ConnectionManagerImpl.
  connection_.write(buffer, false);
  return length;
}

Status ConnectionImpl::onStreamClose(StreamImpl* stream, uint32_t error_code) {
  if (stream) {
    const int32_t stream_id = stream->stream_id_;

    // Consume buffered on stream_close.
    if (stream->stream_manager_.buffered_on_stream_close_) {
      stream->stream_manager_.buffered_on_stream_close_ = false;
      stats_.deferred_stream_close_.dec();
    }

    ENVOY_CONN_LOG(debug, "stream {} closed: {}", connection_, stream_id, error_code);

    if (!stream->remote_end_stream_ || !stream->local_end_stream_) {
      StreamResetReason reason;
      if (stream->reset_due_to_messaging_error_) {
        // Unfortunately, the nghttp2 API makes it incredibly difficult to clearly understand
        // the flow of resets. I.e., did the reset originate locally? Was it remote? Here,
        // we attempt to track cases in which we sent a reset locally due to an invalid frame
        // received from the remote. We only do that in two cases currently (HTTP messaging layer
        // errors from https://tools.ietf.org/html/rfc7540#section-8 which nghttp2 is very strict
        // about). In other cases we treat invalid frames as a protocol error and just kill
        // the connection.

        // Get ClientConnectionImpl or ServerConnectionImpl specific stream reset reason,
        // depending whether the connection is upstream or downstream.
        reason = getMessagingErrorResetReason();
      } else {
        if (error_code == NGHTTP2_REFUSED_STREAM) {
          reason = StreamResetReason::RemoteRefusedStreamReset;
          stream->setDetails(Http2ResponseCodeDetails::get().remote_refused);
        } else {
          if (error_code == NGHTTP2_CONNECT_ERROR) {
            reason = StreamResetReason::ConnectError;
          } else {
            reason = StreamResetReason::RemoteReset;
          }
          stream->setDetails(Http2ResponseCodeDetails::get().remote_reset);
        }
      }

      stream->runResetCallbacks(reason);

    } else if (stream->defer_processing_backedup_streams_ && !stream->reset_reason_.has_value() &&
               stream->stream_manager_.hasBufferedBodyOrTrailers()) {
      ASSERT(error_code == NGHTTP2_NO_ERROR);
      ENVOY_CONN_LOG(debug, "buffered onStreamClose for stream: {}", connection_, stream_id);
      // Buffer the call, rely on the stream->process_buffered_data_callback_
      // to end up invoking.
      stream->stream_manager_.buffered_on_stream_close_ = true;
      stats_.deferred_stream_close_.inc();
      return okStatus();
    }

    stream->destroy();
    current_stream_id_.reset();
    // TODO(antoniovicente) Test coverage for onCloseStream before deferred reset handling happens.
    pending_deferred_reset_streams_.erase(stream->stream_id_);

    connection_.dispatcher().deferredDelete(stream->removeFromList(active_streams_));
    // Any unconsumed data must be consumed before the stream is deleted.
    // nghttp2 does not appear to track this internally, and any stream deleted
    // with outstanding window will contribute to a slow connection-window leak.
    ENVOY_CONN_LOG(debug, "Recouping {} bytes of flow control window for stream {}.", connection_,
                   stream->unconsumed_bytes_, stream_id);
    adapter_->MarkDataConsumedForStream(stream_id, stream->unconsumed_bytes_);
    stream->unconsumed_bytes_ = 0;
    adapter_->SetStreamUserData(stream->stream_id_, nullptr);
  }

  return okStatus();
}

Status ConnectionImpl::onStreamClose(int32_t stream_id, uint32_t error_code) {
  return onStreamClose(getStream(stream_id), error_code);
}

int ConnectionImpl::onMetadataReceived(int32_t stream_id, const uint8_t* data, size_t len) {
  ENVOY_CONN_LOG(trace, "recv {} bytes METADATA", connection_, len);

  StreamImpl* stream = getStream(stream_id);
  if (!stream || stream->remote_end_stream_) {
    return 0;
  }

  bool success = stream->getMetadataDecoder().receiveMetadata(data, len);
  return success ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int ConnectionImpl::onMetadataFrameComplete(int32_t stream_id, bool end_metadata) {
  ENVOY_CONN_LOG(trace, "recv METADATA frame on stream {}, end_metadata: {}", connection_,
                 stream_id, end_metadata);

  StreamImpl* stream = getStream(stream_id);
  if (!stream || stream->remote_end_stream_) {
    return 0;
  }

  bool result = stream->getMetadataDecoder().onMetadataFrameComplete(end_metadata);
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int ConnectionImpl::saveHeader(const nghttp2_frame* frame, HeaderString&& name,
                               HeaderString&& value) {
  StreamImpl* stream = getStream(frame->hd.stream_id);
  if (!stream) {
    // We have seen 1 or 2 crashes where we get a headers callback but there is no associated
    // stream data. I honestly am not sure how this can happen. However, from reading the nghttp2
    // code it looks possible that inflate_header_block() can safely inflate headers for an already
    // closed stream, but will still call the headers callback. Since that seems possible, we should
    // ignore this case here.
    // TODO(mattklein123): Figure out a test case that can hit this.
    stats_.headers_cb_no_stream_.inc();
    return 0;
  }

  // TODO(10646): Switch to use HeaderUtility::checkHeaderNameForUnderscores().
  auto should_return = checkHeaderNameForUnderscores(name.getStringView());
  if (should_return) {
    stream->setDetails(Http2ResponseCodeDetails::get().invalid_underscore);
    name.clear();
    value.clear();
    return should_return.value();
  }

  stream->saveHeader(std::move(name), std::move(value));

  if (stream->headers().byteSize() > max_headers_kb_ * 1024 ||
      stream->headers().size() > max_headers_count_) {
    stream->setDetails(Http2ResponseCodeDetails::get().too_many_headers);
    stats_.header_overflow_.inc();
    // This will cause the library to reset/close the stream.
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  } else {
    return 0;
  }
}

Status ConnectionImpl::sendPendingFrames() {
  if (dispatching_ || connection_.state() == Network::Connection::State::Closed) {
    return okStatus();
  }

  const int rc = adapter_->Send();
  if (rc != 0) {
    ASSERT(rc == NGHTTP2_ERR_CALLBACK_FAILURE);
    return codecProtocolError(nghttp2_strerror(rc));
  }

  // See ConnectionImpl::StreamImpl::resetStream() for why we do this. This is an uncommon event,
  // so iterating through every stream to find the ones that have a deferred reset is not a big
  // deal. Furthermore, queueing a reset frame does not actually invoke the close stream callback.
  // This is only done when the reset frame is sent. Thus, it's safe to work directly with the
  // stream map.
  // NOTE: The way we handle deferred reset is essentially best effort. If we intend to do a
  //       deferred reset, we try to finish the stream, including writing any pending data frames.
  //       If we cannot do this (potentially due to not enough window), we just reset the stream.
  //       In general this behavior occurs only when we are trying to send immediate error messages
  //       to short circuit requests. In the best effort case, we complete the stream before
  //       resetting. In other cases, we just do the reset now which will blow away pending data
  //       frames and release any memory associated with the stream.
  if (!pending_deferred_reset_streams_.empty()) {
    while (!pending_deferred_reset_streams_.empty()) {
      auto it = pending_deferred_reset_streams_.begin();
      auto* stream = it->second;
      // Sanity check: the stream's id matches the map key.
      ASSERT(it->first == stream->stream_id_);
      pending_deferred_reset_streams_.erase(it);
      ASSERT(stream->deferred_reset_);
      stream->resetStreamWorker(stream->deferred_reset_.value());
    }
    RETURN_IF_ERROR(sendPendingFrames());
  }

  // After all pending frames have been written into the outbound buffer check if any of
  // protocol constraints had been violated.
  Status status = protocol_constraints_.checkOutboundFrameLimits();
  if (!status.ok()) {
    ENVOY_CONN_LOG(debug, "error sending frames: Too many frames in the outbound queue.",
                   connection_);
  }
  return status;
}

bool ConnectionImpl::sendPendingFramesAndHandleError() {
  if (!sendPendingFrames().ok()) {
    scheduleProtocolConstraintViolationCallback();
    return true;
  }
  return false;
}

void ConnectionImpl::sendSettingsHelper(
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options, bool disable_push) {
  absl::InlinedVector<http2::adapter::Http2Setting, 10> settings;
  auto insertParameter = [&settings](const http2::adapter::Http2Setting& entry) mutable -> bool {
    // Consider using a set as an intermediate data structure, rather than this ad-hoc
    // deduplication.
    const auto it = std::find_if(
        settings.cbegin(), settings.cend(),
        [&entry](const http2::adapter::Http2Setting& existing) { return entry.id == existing.id; });
    if (it != settings.end()) {
      return false;
    }
    settings.push_back(entry);
    return true;
  };

  // Universally disable receiving push promise frames as we don't currently
  // support them. nghttp2 will fail the connection if the other side still
  // sends them.
  // TODO(mattklein123): Remove this when we correctly proxy push promise.
  // NOTE: This is a special case with respect to custom parameter overrides in
  // that server push is not supported and therefore not end user configurable.
  if (disable_push) {
    settings.push_back({static_cast<int32_t>(http2::adapter::ENABLE_PUSH), disable_push ? 0U : 1U});
  }

  for (const auto& it : http2_options.custom_settings_parameters()) {
    ASSERT(it.identifier().value() <= std::numeric_limits<uint16_t>::max());
    const bool result =
        insertParameter({static_cast<http2::adapter::Http2SettingsId>(it.identifier().value()),
                         it.value().value()});
    ASSERT(result);
    ENVOY_CONN_LOG(debug, "adding custom settings parameter with id {:#x} to {}", connection_,
                   it.identifier().value(), it.value().value());
  }

  // Insert named parameters.
  settings.insert(
      settings.end(),
      {{http2::adapter::HEADER_TABLE_SIZE, http2_options.hpack_table_size().value()},
       {http2::adapter::ENABLE_CONNECT_PROTOCOL, http2_options.allow_connect()},
       {http2::adapter::MAX_CONCURRENT_STREAMS, http2_options.max_concurrent_streams().value()},
       {http2::adapter::INITIAL_WINDOW_SIZE, http2_options.initial_stream_window_size().value()}});
  adapter_->SubmitSettings(settings);
}

void ConnectionImpl::sendSettings(
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options, bool disable_push) {
  sendSettingsHelper(http2_options, disable_push);

  const uint32_t initial_connection_window_size =
      http2_options.initial_connection_window_size().value();
  // Increase connection window size up to our default size.
  if (initial_connection_window_size != NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE) {
    ENVOY_CONN_LOG(debug, "updating connection-level initial window size to {}", connection_,
                   initial_connection_window_size);
    adapter_->SubmitWindowUpdate(0, initial_connection_window_size -
                                        NGHTTP2_INITIAL_CONNECTION_WINDOW_SIZE);
  }
}

int ConnectionImpl::setAndCheckNghttp2CallbackStatus(Status&& status) {
  // Keep the error status that caused the original failure. Subsequent
  // error statuses are silently discarded.
  nghttp2_callback_status_.Update(std::move(status));
  if (nghttp2_callback_status_.ok() && connection_.state() != Network::Connection::State::Open) {
    nghttp2_callback_status_ = codecProtocolError("Connection was closed while dispatching frames");
  }

  return nghttp2_callback_status_.ok() ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

void ConnectionImpl::scheduleProtocolConstraintViolationCallback() {
  if (!protocol_constraint_violation_callback_) {
    protocol_constraint_violation_callback_ = connection_.dispatcher().createSchedulableCallback(
        [this]() { onProtocolConstraintViolation(); });
    protocol_constraint_violation_callback_->scheduleCallbackCurrentIteration();
  }
}

void ConnectionImpl::onProtocolConstraintViolation() {
  // Flooded outbound queue implies that peer is not reading and it does not
  // make sense to try to flush pending bytes.
  connection_.close(Envoy::Network::ConnectionCloseType::NoFlush);
}

void ConnectionImpl::onUnderlyingConnectionBelowWriteBufferLowWatermark() {
  if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams)) {
    // Notify the streams based on least recently encoding to the connection.
    for (auto it = active_streams_.rbegin(); it != active_streams_.rend(); ++it) {
      (*it)->runLowWatermarkCallbacks();
    }
  } else {
    for (auto& stream : active_streams_) {
      stream->runLowWatermarkCallbacks();
    }
  }
}

ConnectionImpl::Http2Callbacks::Http2Callbacks() {
  nghttp2_session_callbacks_new(&callbacks_);
  nghttp2_session_callbacks_set_send_callback(
      callbacks_,
      [](nghttp2_session*, const uint8_t* data, size_t length, int, void* user_data) -> ssize_t {
        return static_cast<ConnectionImpl*>(user_data)->onSend(data, length);
      });

  nghttp2_session_callbacks_set_send_data_callback(
      callbacks_,
      [](nghttp2_session*, nghttp2_frame* frame, const uint8_t* framehd, size_t length,
         nghttp2_data_source* source, void*) -> int {
        ASSERT(frame->data.padlen == 0);
        static_cast<StreamImpl*>(source->ptr)->onDataSourceSend(framehd, length);
        return 0;
      });

  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        auto status = static_cast<ConnectionImpl*>(user_data)->onBeginHeaders(frame);
        return static_cast<ConnectionImpl*>(user_data)->setAndCheckNghttp2CallbackStatus(
            std::move(status));
      });

  nghttp2_session_callbacks_set_on_header_callback(
      callbacks_,
      [](nghttp2_session*, const nghttp2_frame* frame, const uint8_t* raw_name, size_t name_length,
         const uint8_t* raw_value, size_t value_length, uint8_t, void* user_data) -> int {
        // TODO PERF: Can reference count here to avoid copies.
        HeaderString name;
        name.setCopy(reinterpret_cast<const char*>(raw_name), name_length);
        HeaderString value;
        value.setCopy(reinterpret_cast<const char*>(raw_value), value_length);
        return static_cast<ConnectionImpl*>(user_data)->onHeader(frame, std::move(name),
                                                                 std::move(value));
      });

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks_,
      [](nghttp2_session*, uint8_t, int32_t stream_id, const uint8_t* data, size_t len,
         void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onData(stream_id, data, len);
      });

  nghttp2_session_callbacks_set_on_begin_frame_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame_hd* hd, void* user_data) -> int {
        auto status = static_cast<ConnectionImpl*>(user_data)->onBeforeFrameReceived(hd);
        return static_cast<ConnectionImpl*>(user_data)->setAndCheckNghttp2CallbackStatus(
            std::move(status));
      });

  nghttp2_session_callbacks_set_on_frame_recv_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        auto status = static_cast<ConnectionImpl*>(user_data)->onFrameReceived(frame);
        return static_cast<ConnectionImpl*>(user_data)->setAndCheckNghttp2CallbackStatus(
            std::move(status));
      });

  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks_,
      [](nghttp2_session*, int32_t stream_id, uint32_t error_code, void* user_data) -> int {
        auto status = static_cast<ConnectionImpl*>(user_data)->onStreamClose(stream_id, error_code);
        return static_cast<ConnectionImpl*>(user_data)->setAndCheckNghttp2CallbackStatus(
            std::move(status));
      });

  nghttp2_session_callbacks_set_on_frame_send_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onFrameSend(frame);
      });

  nghttp2_session_callbacks_set_before_frame_send_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onBeforeFrameSend(frame);
      });

  nghttp2_session_callbacks_set_on_frame_not_send_callback(
      callbacks_, [](nghttp2_session*, const nghttp2_frame*, int, void*) -> int {
        // We used to always return failure here but it looks now this can get called if the other
        // side sends GOAWAY and we are trying to send a SETTINGS ACK. Just ignore this for now.
        return 0;
      });

  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks_,
      [](nghttp2_session*, const nghttp2_frame* frame, int error_code, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onInvalidFrame(frame->hd.stream_id,
                                                                       error_code);
      });

  nghttp2_session_callbacks_set_on_extension_chunk_recv_callback(
      callbacks_,
      [](nghttp2_session*, const nghttp2_frame_hd* hd, const uint8_t* data, size_t len,
         void* user_data) -> int {
        ASSERT(hd->length >= len);
        return static_cast<ConnectionImpl*>(user_data)->onMetadataReceived(hd->stream_id, data,
                                                                           len);
      });

  nghttp2_session_callbacks_set_unpack_extension_callback(
      callbacks_, [](nghttp2_session*, void**, const nghttp2_frame_hd* hd, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onMetadataFrameComplete(
            hd->stream_id, hd->flags == END_METADATA_FLAG);
      });

  nghttp2_session_callbacks_set_error_callback2(
      callbacks_, [](nghttp2_session*, int, const char* msg, size_t len, void* user_data) -> int {
        return static_cast<ConnectionImpl*>(user_data)->onError(absl::string_view(msg, len));
      });
}

ConnectionImpl::Http2Callbacks::~Http2Callbacks() { nghttp2_session_callbacks_del(callbacks_); }

ConnectionImpl::Http2Options::Http2Options(
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options, uint32_t max_headers_kb) {
  og_options_.perspective = http2::adapter::Perspective::kServer;
  og_options_.max_hpack_encoding_table_capacity = http2_options.hpack_table_size().value();
  og_options_.max_header_list_bytes = max_headers_kb * 1024;
  og_options_.max_header_field_size = max_headers_kb * 1024;
  og_options_.allow_extended_connect = http2_options.allow_connect();

#ifdef ENVOY_ENABLE_UHV
  // UHV - disable header validations in oghttp2
  og_options_.validate_http_headers = false;
#endif

  nghttp2_option_new(&options_);
  // Currently we do not do anything with stream priority. Setting the following option prevents
  // nghttp2 from keeping around closed streams for use during stream priority dependency graph
  // calculations. This saves a tremendous amount of memory in cases where there are a large
  // number of kept alive HTTP/2 connections.
  nghttp2_option_set_no_closed_streams(options_, 1);
  nghttp2_option_set_no_auto_window_update(options_, 1);

  // RFC9113 invalidates trailing whitespace in header values but this is a new validation which
  // can break existing deployments.
  // Disable this validation for now.
  nghttp2_option_set_no_rfc9113_leading_and_trailing_ws_validation(options_, 1);

  // The max send header block length is configured to an arbitrarily high number so as to never
  // trigger the check within nghttp2, as we check request headers length in
  // codec_impl::saveHeader.
  nghttp2_option_set_max_send_header_block_length(options_, 0x2000000);

  if (http2_options.hpack_table_size().value() != NGHTTP2_DEFAULT_HEADER_TABLE_SIZE) {
    nghttp2_option_set_max_deflate_dynamic_table_size(options_,
                                                      http2_options.hpack_table_size().value());
  }

  if (http2_options.allow_metadata()) {
    nghttp2_option_set_user_recv_extension_type(options_, METADATA_FRAME_TYPE);
  } else {
    ENVOY_LOG(trace, "Codec does not have Metadata frame support.");
  }

  // nghttp2 v1.39.2 lowered the internal flood protection limit from 10K to 1K of ACK frames.
  // This new limit may cause the internal nghttp2 mitigation to trigger more often (as it
  // requires just 9K of incoming bytes for smallest 9 byte SETTINGS frame), bypassing the same
  // mitigation and its associated behavior in the envoy HTTP/2 codec. Since envoy does not rely
  // on this mitigation, set back to the old 10K number to avoid any changes in the HTTP/2 codec
  // behavior.
  nghttp2_option_set_max_outbound_ack(options_, 10000);
}

ConnectionImpl::Http2Options::~Http2Options() { nghttp2_option_del(options_); }

ConnectionImpl::ClientHttp2Options::ClientHttp2Options(
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options, uint32_t max_headers_kb)
    : Http2Options(http2_options, max_headers_kb) {
  og_options_.perspective = http2::adapter::Perspective::kClient;
  // Temporarily disable initial max streams limit/protection, since we might want to create
  // more than 100 streams before receiving the HTTP/2 SETTINGS frame from the server.
  //
  // TODO(PiotrSikora): remove this once multiple upstream connections or queuing are implemented.
  nghttp2_option_set_peer_max_concurrent_streams(
      options_, ::Envoy::Http2::Utility::OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS);
}

void ConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "Http2::ConnectionImpl " << this << DUMP_MEMBER(max_headers_kb_)
     << DUMP_MEMBER(max_headers_count_) << DUMP_MEMBER(per_stream_buffer_limit_)
     << DUMP_MEMBER(allow_metadata_) << DUMP_MEMBER(stream_error_on_invalid_http_messaging_)
     << DUMP_MEMBER(is_outbound_flood_monitored_control_frame_) << DUMP_MEMBER(dispatching_)
     << DUMP_MEMBER(raised_goaway_) << DUMP_MEMBER(pending_deferred_reset_streams_.size()) << '\n';

  // Dump the protocol constraints
  DUMP_DETAILS(&protocol_constraints_);

  // Dump either a targeted stream or several of the active streams.
  dumpStreams(os, indent_level);

  // Dump the active slice
  if (current_slice_ == nullptr) {
    // No current slice, use macro for consistent formatting.
    os << spaces << "current_slice_: null\n";
  } else {
    auto slice_view =
        absl::string_view(static_cast<const char*>(current_slice_->mem_), current_slice_->len_);

    os << spaces << "current slice length: " << slice_view.length() << " contents: \"";
    StringUtil::escapeToOstream(os, slice_view);
    os << "\"\n";
  }
}

void ConnectionImpl::dumpStreams(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);

  // Try to dump details for the current stream.
  // If none, dump a subset of our active streams.
  os << spaces << "Number of active streams: " << active_streams_.size()
     << DUMP_OPTIONAL_MEMBER(current_stream_id_);

  if (current_stream_id_.has_value()) {
    os << " Dumping current stream:\n";
    const ConnectionImpl::StreamImpl* stream = getStream(current_stream_id_.value());
    DUMP_DETAILS(stream);
  } else {
    os << " Dumping " << std::min<size_t>(25, active_streams_.size()) << " Active Streams:\n";
    size_t count = 0;
    for (auto& stream : active_streams_) {
      DUMP_DETAILS(stream);
      if (++count >= 25) {
        break;
      }
    }
  }
}

void ClientConnectionImpl::dumpStreams(std::ostream& os, int indent_level) const {
  ConnectionImpl::dumpStreams(os, indent_level);

  if (!current_stream_id_.has_value()) {
    return;
  }

  // Try to dump the downstream request information, corresponding to the
  // stream we were processing.
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "Dumping corresponding downstream request for upstream stream "
     << current_stream_id_.value() << ":\n";

  const ClientStreamImpl* client_stream =
      static_cast<const ClientStreamImpl*>(getStream(current_stream_id_.value()));
  if (client_stream) {
    client_stream->response_decoder_.dumpState(os, indent_level + 1);
  } else {
    os << spaces
       << " Failed to get the upstream stream with stream id: " << current_stream_id_.value()
       << " Unable to dump downstream request.\n";
  }
}

void ConnectionImpl::StreamImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "ConnectionImpl::StreamImpl " << this << DUMP_MEMBER(stream_id_)
     << DUMP_MEMBER(unconsumed_bytes_) << DUMP_MEMBER(read_disable_count_)
     << DUMP_MEMBER(local_end_stream_) << DUMP_MEMBER(local_end_stream_sent_)
     << DUMP_MEMBER(remote_end_stream_) << DUMP_MEMBER(data_deferred_)
     << DUMP_MEMBER(received_noninformational_headers_)
     << DUMP_MEMBER(pending_receive_buffer_high_watermark_called_)
     << DUMP_MEMBER(pending_send_buffer_high_watermark_called_)
     << DUMP_MEMBER(reset_due_to_messaging_error_)
     << DUMP_MEMBER_AS(cookies_, cookies_.getStringView());

  DUMP_DETAILS(pending_trailers_to_encode_);
}

void ConnectionImpl::ClientStreamImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  StreamImpl::dumpState(os, indent_level);

  // Dump header map
  if (absl::holds_alternative<ResponseHeaderMapPtr>(headers_or_trailers_)) {
    DUMP_DETAILS(absl::get<ResponseHeaderMapPtr>(headers_or_trailers_));
  } else {
    DUMP_DETAILS(absl::get<ResponseTrailerMapPtr>(headers_or_trailers_));
  }
}

void ConnectionImpl::ServerStreamImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  StreamImpl::dumpState(os, indent_level);

  // Dump header map
  if (absl::holds_alternative<RequestHeaderMapPtr>(headers_or_trailers_)) {
    DUMP_DETAILS(absl::get<RequestHeaderMapPtr>(headers_or_trailers_));
  } else {
    DUMP_DETAILS(absl::get<RequestTrailerMapPtr>(headers_or_trailers_));
  }
}

ClientConnectionImpl::ClientConnectionImpl(
    Network::Connection& connection, Http::ConnectionCallbacks& callbacks, CodecStats& stats,
    Random::RandomGenerator& random_generator,
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
    const uint32_t max_response_headers_kb, const uint32_t max_response_headers_count,
    Http2SessionFactory& http2_session_factory)
    : ConnectionImpl(connection, stats, random_generator, http2_options, max_response_headers_kb,
                     max_response_headers_count),
      callbacks_(callbacks) {
  ClientHttp2Options client_http2_options(http2_options, max_response_headers_kb);
  if (use_oghttp2_library_) {
    adapter_ = http2_session_factory.create(http2_callbacks_.callbacks(), base(),
                                            client_http2_options.ogOptions());
  } else {
    adapter_ = http2_session_factory.create(http2_callbacks_.callbacks(), base(),
                                            client_http2_options.options());
  }
  http2_session_factory.init(base(), http2_options);
  allow_metadata_ = http2_options.allow_metadata();
  idle_session_requires_ping_interval_ = std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
      http2_options.connection_keepalive(), connection_idle_interval, 0));
}

RequestEncoder& ClientConnectionImpl::newStream(ResponseDecoder& decoder) {
  // If the connection has been idle long enough to trigger a ping, send one
  // ahead of creating the stream.
  if (idle_session_requires_ping_interval_.count() != 0 &&
      (connection_.dispatcher().timeSource().monotonicTime() - lastReceivedDataTime() >
       idle_session_requires_ping_interval_)) {
    sendKeepalive();
  }

  ClientStreamImplPtr stream(new ClientStreamImpl(*this, per_stream_buffer_limit_, decoder));
  // If the connection is currently above the high watermark, make sure to inform the new stream.
  // The connection can not pass this on automatically as it has no awareness that a new stream is
  // created.
  if (connection_.aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
  ClientStreamImpl& stream_ref = *stream;
  LinkedList::moveIntoList(std::move(stream), active_streams_);
  protocol_constraints_.incrementOpenedStreamCount();
  return stream_ref;
}

Status ClientConnectionImpl::onBeginHeaders(const nghttp2_frame* frame) {
  // The client code explicitly does not currently support push promise.
  RELEASE_ASSERT(frame->hd.type == NGHTTP2_HEADERS, "");
  RELEASE_ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE ||
                     frame->headers.cat == NGHTTP2_HCAT_HEADERS,
                 "");
  RETURN_IF_ERROR(trackInboundFrames(&frame->hd, frame->headers.padlen));
  if (frame->headers.cat == NGHTTP2_HCAT_HEADERS) {
    StreamImpl* stream = getStream(frame->hd.stream_id);
    stream->allocTrailers();
  }

  return okStatus();
}

int ClientConnectionImpl::onHeader(const nghttp2_frame* frame, HeaderString&& name,
                                   HeaderString&& value) {
  // The client code explicitly does not currently support push promise.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(frame->headers.cat == NGHTTP2_HCAT_RESPONSE || frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  ASSERT(connection_.state() == Network::Connection::State::Open);
  return saveHeader(frame, std::move(name), std::move(value));
}

// TODO(yanavlasov): move to the base class once the runtime flag is removed.
Status ClientConnectionImpl::trackInboundFrames(const nghttp2_frame_hd* hd,
                                                uint32_t padding_length) {
  Status result;
  ENVOY_CONN_LOG(trace, "track inbound frame type={} flags={} length={} padding_length={}",
                 connection_, static_cast<uint64_t>(hd->type), static_cast<uint64_t>(hd->flags),
                 static_cast<uint64_t>(hd->length), padding_length);

  result = protocol_constraints_.trackInboundFrames(hd, padding_length);
  if (!result.ok()) {
    ENVOY_CONN_LOG(trace, "error reading frame: {} received in this HTTP/2 session.", connection_,
                   result.message());
    if (isInboundFramesWithEmptyPayloadError(result)) {
      ConnectionImpl::StreamImpl* stream = getStream(hd->stream_id);
      if (stream) {
        stream->setDetails(Http2ResponseCodeDetails::get().inbound_empty_frame_flood);
      }
    }
  }
  return result;
}

StreamResetReason ClientConnectionImpl::getMessagingErrorResetReason() const {
  connection_.streamInfo().setResponseFlag(StreamInfo::ResponseFlag::UpstreamProtocolError);

  return StreamResetReason::ProtocolError;
}

ServerConnectionImpl::ServerConnectionImpl(
    Network::Connection& connection, Http::ServerConnectionCallbacks& callbacks, CodecStats& stats,
    Random::RandomGenerator& random_generator,
    const envoy::config::core::v3::Http2ProtocolOptions& http2_options,
    const uint32_t max_request_headers_kb, const uint32_t max_request_headers_count,
    envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
        headers_with_underscores_action)
    : ConnectionImpl(connection, stats, random_generator, http2_options, max_request_headers_kb,
                     max_request_headers_count),
      callbacks_(callbacks), headers_with_underscores_action_(headers_with_underscores_action) {
  Http2Options h2_options(http2_options, max_request_headers_kb);

  auto visitor = std::make_unique<http2::adapter::CallbackVisitor>(
      http2::adapter::Perspective::kServer, *http2_callbacks_.callbacks(), base());
  if (use_oghttp2_library_) {
    visitor_ = std::move(visitor);
    adapter_ = http2::adapter::OgHttp2Adapter::Create(*visitor_, h2_options.ogOptions());
  } else {
    auto adapter =
        http2::adapter::NgHttp2Adapter::CreateServerAdapter(*visitor, h2_options.options());
    auto stream_close_listener = [p = adapter.get()](http2::adapter::Http2StreamId stream_id) {
      p->RemoveStream(stream_id);
    };
    visitor->set_stream_close_listener(std::move(stream_close_listener));
    visitor_ = std::move(visitor);
    adapter_ = std::move(adapter);
  }
  sendSettings(http2_options, false);
  allow_metadata_ = http2_options.allow_metadata();
}

Status ServerConnectionImpl::onBeginHeaders(const nghttp2_frame* frame) {
  // For a server connection, we should never get push promise frames.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(connection_.state() == Network::Connection::State::Open);
  RETURN_IF_ERROR(trackInboundFrames(&frame->hd, frame->headers.padlen));

  if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
    stats_.trailers_.inc();
    ASSERT(frame->headers.cat == NGHTTP2_HCAT_HEADERS);

    StreamImpl* stream = getStream(frame->hd.stream_id);
    stream->allocTrailers();
    return okStatus();
  }

  ServerStreamImplPtr stream(new ServerStreamImpl(*this, per_stream_buffer_limit_));
  if (connection_.aboveHighWatermark()) {
    stream->runHighWatermarkCallbacks();
  }
  stream->request_decoder_ = &callbacks_.newStream(*stream);
  stream->stream_id_ = frame->hd.stream_id;
  LinkedList::moveIntoList(std::move(stream), active_streams_);
  adapter_->SetStreamUserData(frame->hd.stream_id, active_streams_.front().get());
  protocol_constraints_.incrementOpenedStreamCount();
  return okStatus();
}

int ServerConnectionImpl::onHeader(const nghttp2_frame* frame, HeaderString&& name,
                                   HeaderString&& value) {
  // For a server connection, we should never get push promise frames.
  ASSERT(frame->hd.type == NGHTTP2_HEADERS);
  ASSERT(frame->headers.cat == NGHTTP2_HCAT_REQUEST || frame->headers.cat == NGHTTP2_HCAT_HEADERS);
  return saveHeader(frame, std::move(name), std::move(value));
}

Status ServerConnectionImpl::trackInboundFrames(const nghttp2_frame_hd* hd,
                                                uint32_t padding_length) {
  ENVOY_CONN_LOG(trace, "track inbound frame type={} flags={} length={} padding_length={}",
                 connection_, static_cast<uint64_t>(hd->type), static_cast<uint64_t>(hd->flags),
                 static_cast<uint64_t>(hd->length), padding_length);

  auto result = protocol_constraints_.trackInboundFrames(hd, padding_length);
  if (!result.ok()) {
    ENVOY_CONN_LOG(trace, "error reading frame: {} received in this HTTP/2 session.", connection_,
                   result.message());
    if (isInboundFramesWithEmptyPayloadError(result)) {
      ConnectionImpl::StreamImpl* stream = getStream(hd->stream_id);
      if (stream) {
        stream->setDetails(Http2ResponseCodeDetails::get().inbound_empty_frame_flood);
      }
    }
  }
  return result;
}

Http::Status ServerConnectionImpl::dispatch(Buffer::Instance& data) {
  // Make sure downstream outbound queue was not flooded by the upstream frames.
  RETURN_IF_ERROR(protocol_constraints_.checkOutboundFrameLimits());
  return ConnectionImpl::dispatch(data);
}

absl::optional<int>
ServerConnectionImpl::checkHeaderNameForUnderscores(absl::string_view header_name) {
  if (headers_with_underscores_action_ != envoy::config::core::v3::HttpProtocolOptions::ALLOW &&
      Http::HeaderUtility::headerNameContainsUnderscore(header_name)) {
    if (headers_with_underscores_action_ ==
        envoy::config::core::v3::HttpProtocolOptions::DROP_HEADER) {
      ENVOY_CONN_LOG(debug, "Dropping header with invalid characters in its name: {}", connection_,
                     header_name);
      stats_.dropped_headers_with_underscores_.inc();
      return 0;
    }
    ENVOY_CONN_LOG(debug, "Rejecting request due to header name with underscores: {}", connection_,
                   header_name);
    stats_.requests_rejected_with_underscores_in_headers_.inc();
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }
  return absl::nullopt;
}

} // namespace Http2
} // namespace Http
} // namespace Envoy
