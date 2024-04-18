#pragma once

#include "quiche/quic/core/quic_stream.h"

namespace Envoy {
namespace Quic {

// An interface for stream and connection to update send buffer watermark.
class SendBufferMonitor {
public:
  virtual ~SendBufferMonitor() = default;

  // A scoped object to update the send buffer size change of the given quic stream to
  // SendBufferMonitor during its life time. If the given send buffer monitor is already monitoring,
  // skip updating the monitor after it's out of scope because this updater is in the scope of
  // another one.
  class ScopedWatermarkBufferUpdater {
  public:
    ScopedWatermarkBufferUpdater(quic::QuicStream* quic_stream,
                                 SendBufferMonitor* send_buffer_monitor);

    ~ScopedWatermarkBufferUpdater();

  private:
    quic::QuicStream* quic_stream_{nullptr};
    uint64_t old_buffered_bytes_{0};
    SendBufferMonitor* send_buffer_monitor_{nullptr};
  };

protected:
  // Update the monitor with the new buffered bytes and check watermark threshold.
  virtual void updateBytesBuffered(uint64_t old_buffered_bytes, uint64_t new_buffered_bytes) = 0;

  bool isDoingWatermarkAccounting() const { return is_doing_watermark_accounting_; }

private:
  bool is_doing_watermark_accounting_{false};
};

} // namespace Quic
} // namespace Envoy
