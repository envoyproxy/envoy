#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/http/codec.h"

#include "common/http/codec_helper.h"

#include "extensions/quic_listeners/quiche/envoy_quic_simulated_watermark_buffer.h"

namespace Envoy {
namespace Quic {

// Base class for EnvoyQuicServer|ClientStream.
class EnvoyQuicStream : public Http::StreamEncoder,
                        public Http::Stream,
                        public Http::StreamCallbackHelper,
                        protected Logger::Loggable<Logger::Id::quic_stream> {
public:
  EnvoyQuicStream(uint32_t buffer_limit, std::function<void()> below_low_watermark,
                  std::function<void()> above_high_watermark)
      : send_buffer_simulation_(buffer_limit / 2, buffer_limit, std::move(below_low_watermark),
                                std::move(above_high_watermark), ENVOY_LOGGER()) {}

  // Http::StreamEncoder
  Stream& getStream() override { return *this; }

  // Http::Stream
  void readDisable(bool disable) override {
    bool status_changed{false};
    if (disable) {
      ++read_disable_counter_;
      ASSERT(read_disable_counter_ == 1);
      if (read_disable_counter_ == 1) {
        status_changed = true;
      }
    } else {
      ASSERT(read_disable_counter_ > 0);
      --read_disable_counter_;
      if (read_disable_counter_ == 0) {
        status_changed = true;
      }
    }

    if (status_changed && !in_encode_data_callstack_) {
      switchStreamBlockState(disable);
    }
  }

  void addCallbacks(Http::StreamCallbacks& callbacks) override {
    ASSERT(!local_end_stream_);
    addCallbacks_(callbacks);
  }
  void removeCallbacks(Http::StreamCallbacks& callbacks) override { removeCallbacks_(callbacks); }
  uint32_t bufferLimit() override { return quic::kStreamReceiveWindowLimit; }

  // Needs to be called during quic stream creation before the stream receives
  // any headers and data.
  void setDecoder(Http::StreamDecoder& decoder) { decoder_ = &decoder; }

protected:
  virtual void switchStreamBlockState(bool should_block) PURE;

  // Needed for ENVOY_STREAM_LOG.
  virtual uint32_t streamId() PURE;
  virtual Network::Connection* connection() PURE;

  Http::StreamDecoder* decoder() {
    ASSERT(decoder_ != nullptr);
    return decoder_;
  }

  EnvoyQuicSimulatedWatermarkBuffer& sendBufferSimulation() { return send_buffer_simulation_; }
  bool end_stream_decoded_{false};
  int32_t read_disable_counter_{0};
  // If true, switchStreamBlockState() should be deferred till this variable
  // becomes false.
  bool in_encode_data_callstack_{false};

private:
  // Not owned.
  Http::StreamDecoder* decoder_{nullptr};
  EnvoyQuicSimulatedWatermarkBuffer send_buffer_simulation_;
};

} // namespace Quic
} // namespace Envoy
