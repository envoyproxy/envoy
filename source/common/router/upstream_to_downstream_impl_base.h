#pragma once

#include <memory>

#include "envoy/router/router.h"

#include "source/common/http/response_decoder_impl_base.h"

namespace Envoy {
namespace Router {

class UpstreamToDownstreamImplBase : public UpstreamToDownstream {
public:
  UpstreamToDownstreamImplBase() : live_trackable_(std::make_shared<bool>(true)) {}

  Http::ResponseDecoderHandlePtr createResponseDecoderHandle() override {
    return std::make_unique<Http::ResponseDecoderHandleImpl>(live_trackable_, *this);
  }

private:
  std::shared_ptr<bool> live_trackable_;
};

} // namespace Router
} // namespace Envoy
