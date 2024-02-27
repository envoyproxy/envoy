#include "source/common/quic/envoy_quic_stream.h"

#include "source/common/http/utility.h"

#include "quiche/quic/core/http/http_encoder.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/qpack/qpack_instruction_encoder.h"

namespace Envoy {
namespace Quic {

void EnvoyQuicStream::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_STREAM_LOG(debug, "encodeData (end_stream={}) of {} bytes.", *this, end_stream,
                   data.length());
  const bool has_data = data.length() > 0;
  if (!has_data && !end_stream) {
    return;
  }
  if (quic_stream_.write_side_closed()) {
    IS_ENVOY_BUG("encodeData is called on write-closed stream.");
    return;
  }
  ASSERT(!local_end_stream_);
  local_end_stream_ = end_stream;
  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(&quic_stream_, this);
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  if (http_datagram_handler_) {
    IncrementalBytesSentTracker tracker(quic_stream_, *mutableBytesMeter(), false);
    if (!http_datagram_handler_->encodeCapsuleFragment(data.toString(), end_stream)) {
      quic_stream_.Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
      return;
    }
  } else {
#endif
    Buffer::RawSliceVector raw_slices = data.getRawSlices();
    absl::InlinedVector<quiche::QuicheMemSlice, 4> quic_slices;
    quic_slices.reserve(raw_slices.size());
    for (auto& slice : raw_slices) {
      ASSERT(slice.len_ != 0);
      // Move each slice into a stand-alone buffer.
      // TODO(danzh): investigate the cost of allocating one buffer per slice.
      // If it turns out to be expensive, add a new function to free data in the middle in buffer
      // interface and re-design QuicheMemSliceImpl.
      auto single_slice_buffer = std::make_unique<Buffer::OwnedImpl>();
      single_slice_buffer->move(data, slice.len_);
      quic_slices.emplace_back(
          reinterpret_cast<char*>(slice.mem_), slice.len_,
          [single_slice_buffer = std::move(single_slice_buffer)](const char*) mutable {
            // Free this memory explicitly when the callback is invoked.
            single_slice_buffer = nullptr;
          });
    }
    quic::QuicConsumedData result{0, false};
    absl::Span<quiche::QuicheMemSlice> span(quic_slices);
    {
      IncrementalBytesSentTracker tracker(quic_stream_, *mutableBytesMeter(), false);
      result = quic_stream_.WriteBodySlices(span, end_stream);
      if (stats_gatherer_ != nullptr) {
        stats_gatherer_->addBytesSent(result.bytes_consumed, end_stream);
      }
    }
    // QUIC stream must take all.
    if (result.bytes_consumed == 0 && has_data) {
      IS_ENVOY_BUG(fmt::format("Send buffer didn't take all the data. Stream is write {} with {} "
                               "bytes in send buffer. Current write was rejected.",
                               quic_stream_.write_side_closed() ? "closed" : "open",
                               quic_stream_.BufferedDataBytes()));
      quic_stream_.Reset(quic::QUIC_BAD_APPLICATION_PAYLOAD);
      return;
    }
#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
  }
#endif
  if (local_end_stream_) {
    if (codec_callbacks_) {
      codec_callbacks_->onCodecEncodeComplete();
    }
    onLocalEndStream();
  }
}

void EnvoyQuicStream::encodeTrailersImpl(spdy::Http2HeaderBlock&& trailers) {
  if (quic_stream_.write_side_closed()) {
    IS_ENVOY_BUG("encodeTrailers is called on write-closed stream.");
    return;
  }
  ASSERT(!local_end_stream_);
  local_end_stream_ = true;

  SendBufferMonitor::ScopedWatermarkBufferUpdater updater(&quic_stream_, this);
  {
    IncrementalBytesSentTracker tracker(quic_stream_, *mutableBytesMeter(), true);
    size_t bytes_sent = quic_stream_.WriteTrailers(std::move(trailers), nullptr);
    ENVOY_BUG(bytes_sent != 0, "Failed to encode trailers.");
    if (stats_gatherer_ != nullptr) {
      stats_gatherer_->addBytesSent(bytes_sent, true);
    }
  }
  if (codec_callbacks_) {
    codec_callbacks_->onCodecEncodeComplete();
  }
  onLocalEndStream();
}

void EnvoyQuicStream::encodeMetadata(const Http::MetadataMapVector& /*metadata_map_vector*/) {
  // Metadata Frame is not supported in QUICHE.
  ENVOY_STREAM_LOG(debug, "METADATA is not supported in Http3.", *this);
  stats_.metadata_not_supported_error_.inc();
}

} // namespace Quic
} // namespace Envoy
