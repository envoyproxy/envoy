#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using Http::FilterHeadersStatus;

Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  state_ = State::Calling;
  initiating_call_ = true;
  client_->fetchToken(*this);
  initiating_call_ = false;
  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopIteration;
}

void GcpAuthnFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GcpAuthnFilter::onComplete(const Http::ResponseMessage*) {
  state_ = State::Complete;
  if (!initiating_call_) {
    ENVOY_LOG(error, "continue decoding");
    decoder_callbacks_->continueDecoding();
  }

  // TODO(tyxia) Decode jwt token to get the exp time for cache.
}

void GcpAuthnFilter::onDestroy() {
  if (state_ == State::Calling) {
    state_ = State::Complete;
    client_->cancel();
  }
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
