#include "source/common/quic/send_buffer_monitor.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Quic {

SendBufferMonitor::ScopedWatermarkBufferUpdater::ScopedWatermarkBufferUpdater(
    quic::QuicStream* quic_stream, SendBufferMonitor* send_buffer_monitor)
    : quic_stream_(quic_stream), old_buffered_bytes_(quic_stream_->BufferedDataBytes()),
      send_buffer_monitor_(send_buffer_monitor) {
  ASSERT(!send_buffer_monitor_->is_doing_watermark_accounting_);
  send_buffer_monitor_->is_doing_watermark_accounting_ = true;
}

SendBufferMonitor::ScopedWatermarkBufferUpdater::~ScopedWatermarkBufferUpdater() {
  // If quic_stream_ is done writing, regards all buffered bytes, if there is any, as drained.
  uint64_t new_buffered_bytes =
      quic_stream_->write_side_closed() ? 0u : quic_stream_->BufferedDataBytes();
  send_buffer_monitor_->is_doing_watermark_accounting_ = false;
  send_buffer_monitor_->updateBytesBuffered(old_buffered_bytes_, new_buffered_bytes);
}

} // namespace Quic
} // namespace Envoy
