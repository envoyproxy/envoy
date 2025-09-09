#pragma once

#include <memory>

#include "envoy/http/codec.h"

namespace Envoy {
namespace Http {

// Object to track the liveness of the ResponseDecoder.
struct ResponseDecoderLiveTrackable {};

class ResponseDecoderHandleImpl : public ResponseDecoderHandle {
public:
  ResponseDecoderHandleImpl(std::weak_ptr<ResponseDecoderLiveTrackable> live_trackable,
                            ResponseDecoder& decoder)
      : live_trackable_(live_trackable), decoder_(decoder) {}

  OptRef<ResponseDecoder> get() override {
    if (live_trackable_.lock()) {
      return decoder_;
    }
    return {};
  }

private:
  std::weak_ptr<ResponseDecoderLiveTrackable> live_trackable_;
  ResponseDecoder& decoder_;
};

class ResponseDecoderImplBase : public ResponseDecoder {
public:
  ResponseDecoderImplBase() : live_trackable_(std::make_shared<ResponseDecoderLiveTrackable>()) {}

  ResponseDecoderHandlePtr getResponseDecoderHandle() override {
    return std::make_unique<ResponseDecoderHandleImpl>(live_trackable_, *this);
  }

protected:
  std::shared_ptr<ResponseDecoderLiveTrackable> live_trackable_;
};

} // namespace Http
} // namespace Envoy
