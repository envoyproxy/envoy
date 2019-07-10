#include "extensions/quic_listeners/quiche/envoy_quic_server_stream.h"
#include "/usr/local/google/home/danzh/.cache/bazel/_bazel_danzh/3af5f831530d3ae92cc2833051a9b35d/execroot/envoy/bazel-out/k8-fastbuild/bin/external/com_googlesource_quiche/quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "/usr/local/google/home/danzh/.cache/bazel/_bazel_danzh/3af5f831530d3ae92cc2833051a9b35d/execroot/envoy/bazel-out/k8-fastbuild/bin/include/envoy/http/_virtual_includes/codec_interface/envoy/http/codec.h"
#include <cstddef>
#include <openssl/evp.h>

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/spdy/core/spdy_header_block.h"

#pragma GCC diagnostic pop

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"
#include "common/common/assert.h"

namespace Envoy {
namespace Quic {

void EnvoyQuicServerStream::encode100ContinueHeaders(const Http::HeaderMap& /*headers*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicServerStream::encodeHeaders(const Http::HeaderMap& /*headers*/, bool /*end_stream*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicServerStream::encodeData(Buffer::Instance& /*data*/, bool /*end_stream*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicServerStream::encodeTrailers(const Http::HeaderMap& /*trailers*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}
void EnvoyQuicServerStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void EnvoyQuicServerStream::resetStream(Http::StreamResetReason /*reason*/) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void EnvoyQuicServerStream::readDisable(bool /*disable*/) { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

Http::HeaderMapImplPtr quicHeadersToEnvoyHeaders(const quic::QuicHeaderList& header_list) {
  Http::HeaderMapImplPtr headers = std::make_unique<Http::HeaderMapImpl>();
  for (auto entry : header_list) {
    // TODO(danzh): Avoid copy by referencing entry as header_list is already validated by QUIC.
    headers->addCopy(Http::LowerCaseString(entry.first), entry.second);
  }
  return headers;
}

Http::HeaderMapImplPtr spdyHeaderBlockToEnvoyHeaders(const spdy::SpdyHeaderBlock& header_block) {
  Http::HeaderMapImplPtr headers = std::make_unique<Http::HeaderMapImpl>();
  for (auto entry : header_block) {
    // TODO(danzh): Avoid temporary strings and addCopy() with std::string_view.
    std::string key(entry.first);
    std::string value(entry.second);
    headers->addCopy(Http::LowerCaseString(key), value);
  }
  return headers;
}

void EnvoyQuicServerStream::OnInitialHeadersComplete(bool fin, size_t frame_len,
                                                     const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyServerStreamBase::OnInitialHeadersComplete(fin, frame_len, header_list);
  ASSERT(decoder() != nullptr);
  ASSERT(headers_decompressed());
  decoder()->decodeHeaders(quicHeadersToEnvoyHeaders(header_list), /*end_stream=*/fin);
  ConsumeHeaderList();
}

void EnvoyQuicServerStream::OnBodyAvailable() {
  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  // TODO(danzh): check Envoy per stream buffer limit.
  // Currently read out all the data.
  uint64_t bytes_to_read = ReadableBytes();
  if (bytes_to_read > 0) {
  constexpr uint64_t MaxSlices = 2;
  Buffer::RawSlice slices[MaxSlices];
  const uint64_t num_slices = buffer->reserve(bytes_to_read, slices, MaxSlices);
  STACK_ARRAY(iov, iovec, num_slices);
  uint64_t max_len = 0;
  for (uint64_t i = 0; i < num_slices; ++i) {
    Buffer::RawSlice& slice = slices[i];
    iov[i].iov_base = slice.mem_;
    iov[i].iov_len = slice.len_;
    max_len += slice.len_;
  }
  ASSERT(max_len >= bytes_to_read);
  size_t bytes_read = Readv(iov.begin(), num_slices);
  ASSERT(bytes_read == bytes_to_read);
  buffer->commit(slices, num_slices);
  }
  // True if no trailer and FIN read.
  bool finished_reading = IsDoneReading();
  // If this is the last stream data, set end_stream if there is no
  // trailers.
  ASSERT(decoder() != nullptr);
  decoder()->decodeData(*buffer, finished_reading);
  if (sequencer()->IsClosed() && !FinishedReadingTrailers()) {
    decoder()->decodeTrailers(spdyHeaderBlockToEnvoyHeaders(received_trailers()));
    MarkTrailersConsumed();
  }
}

void EnvoyQuicServerStream::OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                                      const quic::QuicHeaderList& header_list) {
  quic::QuicSpdyServerStreamBase::OnTrailingHeadersComplete(fin, frame_len, header_list);
  if (!FinishedReadingHeaders()) {
    // Before QPack trailers can arrive before body. Only decode trailers after finishing decoding body.
    ASSERT(decoder() != nullptr);
    decoder()->decodeTrailers(spdyHeaderBlockToEnvoyHeaders(received_trailers()));
    MarkTrailersConsumed();
  }
}

Http::StreamResetReason quicRstErrorToEnvoyResetReason(quic::QuicRstStreamErrorCode quic_rst) {
  switch(quic_rst) {
    case quic::QUIC_REFUSED_STREAM:
      return  Http::StreamResetReason::RemoteRefusedStreamReset;
    case quic::QUIC_STREAM_NO_ERROR:
      return Http::StreamResetReason:: ConnectionTermination;
    case quic::QUIC_STREAM_CONNECTION_ERROR:
      return Http::StreamResetReason::ConnectionFailure;
    default:
      return Http::StreamResetReason::RemoteReset;
  }
}

void EnvoyQuicServerStream::OnStreamReset(const quic::QuicRstStreamFrame& frame) {
  quic::QuicSpdyServerStreamBase::OnStreamReset(frame);
  Http::StreamResetReason reason = quicRstErrorToEnvoyResetReason(frame.error_code);
  runResetCallbacks(reason);
}

void EnvoyQuicServerStream::OnConnectionClosed(quic::QuicErrorCode error, quic::ConnectionCloseSource source) {
  quic::QuicSpdyServerStreamBase::OnConnectionClosed(error, source);
  Http::StreamResetReason reason = quicRstErrorToEnvoyResetReason(stream_error());
  runResetCallbacks(reason);
}

} // namespace Quic
} // namespace Envoy
