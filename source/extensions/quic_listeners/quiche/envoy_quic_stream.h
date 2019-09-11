#pragma once

#include "envoy/http/codec.h"

#include "common/http/codec_helper.h"

namespace Envoy {
namespace Quic {

// Base class for EnvoyQuicServer|ClientStream.
class EnvoyQuicStream : public Http::StreamEncoder,
                        public Http::Stream,
                        public Http::StreamCallbackHelper {
public:
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

private:
  // Not owned.
  Http::StreamDecoder* decoder_{nullptr};
};

} // namespace Quic
} // namespace Envoy
