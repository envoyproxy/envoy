#pragma once

#include <memory>

#include "envoy/http/codec.h"

namespace Envoy {
namespace Http {

class ResponseDecoderHandleImpl : public ResponseDecoderHandle {
public:
  ResponseDecoderHandleImpl(std::weak_ptr<bool> live_trackable, ResponseDecoder& decoder)
      : live_trackable_(live_trackable), decoder_(decoder) {}

  OptRef<ResponseDecoder> get() override {
    if (live_trackable_.lock()) {
      return decoder_;
    }
    return {};
  }

private:
  std::weak_ptr<bool> live_trackable_;
  ResponseDecoder& decoder_;
};

class ResponseDecoderImplBase : public ResponseDecoder {
public:
  ResponseDecoderImplBase() : live_trackable_(std::make_shared<bool>(true)) {}

  ResponseDecoderHandlePtr createResponseDecoderHandle() override {
    return std::make_unique<ResponseDecoderHandleImpl>(live_trackable_, *this);
  }

private:
  std::shared_ptr<bool> live_trackable_;
};

} // namespace Http
} // namespace Envoy
