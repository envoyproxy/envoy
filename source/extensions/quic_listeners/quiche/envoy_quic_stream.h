#pragma once

#include "envoy/http/codec.h"

#include "common/http/codec_helper.h"

#include "extensions/quic_listeners/quiche/envoy_quic_simulated_watermark_buffer.h"

namespace Envoy {
namespace Quic {

// Base class for EnvoyQuicServer|ClientStream.
class EnvoyQuicStream : public Http::StreamEncoder,
                        public Http::Stream,
                        public Http::StreamCallbackHelper {
public:
  EnvoyQuicStream(uint32_t buffer_limit, std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark)
      : send_buffer_simulation_(buffer_limit / 2, buffer_limit, std::move(below_low_watermark),
                                std::move(above_high_watermark)) {}

  // Http::StreamEncoder
  Stream& getStream() override { return *this; }

  // Http::Stream
  void addCallbacks(Http::StreamCallbacks& callbacks) override {
    ASSERT(!local_end_stream_);
    addCallbacks_(callbacks);
  }
  void removeCallbacks(Http::StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
  uint32_t bufferLimit() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

  // Needs to be called during quic stream creation before the stream receives
  // any headers and data.
  void setDecoder(Http::StreamDecoder& decoder) { decoder_ = &decoder; }

protected:
  Http::StreamDecoder* decoder() {
    ASSERT(decoder_ != nullptr);
    return decoder_;
  }

  EnvoyQuicSimulatedWatermarkBuffer& sendBufferSimulation() { return send_buffer_simulation_; }

private:
  // Not owned.
  Http::StreamDecoder* decoder_{nullptr};
  EnvoyQuicSimulatedWatermarkBuffer send_buffer_simulation_;
};

} // namespace Quic
} // namespace Envoy
