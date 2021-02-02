#include "extensions/quic_listeners/quiche/send_buffer_monitor.h"

namespace Envoy {
namespace Quic {

SendBufferMonitor::ScopedWatermarkBufferUpdater::ScopedWatermarkBufferUpdater(
    quic::QuicStream* quic_stream, SendBufferMonitor* send_buffer_monitor)
    : quic_stream_(quic_stream), old_buffered_bytes_(quic_stream_->BufferedDataBytes()),
      send_buffer_monitor_(send_buffer_monitor) {
  if (!send_buffer_monitor_->is_doing_watermark_accounting_) {
    send_buffer_monitor_->is_doing_watermark_accounting_ = true;
    count_bytes_ = true;
  } else {
    ASSERT(static_cast<void*>(quic_stream) != static_cast<void*>(send_buffer_monitor));
    count_bytes_ = false;
  }
}

SendBufferMonitor::ScopedWatermarkBufferUpdater::~ScopedWatermarkBufferUpdater() {
  if (!count_bytes_) {
    ASSERT(static_cast<void*>(quic_stream_) != static_cast<void*>(send_buffer_monitor_));
    return;
  }
  uint64_t new_buffered_bytes = quic_stream_->BufferedDataBytes();
  send_buffer_monitor_->is_doing_watermark_accounting_ = false;
  send_buffer_monitor_->updateBytesBuffered(old_buffered_bytes_, new_buffered_bytes);
}

} // namespace Quic
} // namespace Envoy
