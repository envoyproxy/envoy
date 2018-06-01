#include "extensions/filters/http/jwt_authn/filter.h"

#include "common/http/utility.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

Filter::Filter(JwtAuthnFilterStats& stats, AuthenticatorPtr auth)
    : stats_(stats), auth_(std::move(auth)) {}

void Filter::onDestroy() {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  if (auth_) {
    auth_->onDestroy();
  }
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);

  // Remove headers configured to pass payload
  auth_->sanitizePayloadHeaders(headers);

  state_ = Calling;
  stopped_ = false;

  // TODO(qiwzhang): support per-route config.
  // Verify the JWT token, onComplete() will be called when completed.
  auth_->verify(headers, this);
  if (state_ == Complete) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_LOG(debug, "Called Filter : {} Stop", __func__);
  stopped_ = true;
  return Http::FilterHeadersStatus::StopIteration;
}

void Filter::onComplete(const Status& status) {
  ENVOY_LOG(debug, "Called Filter : check complete {}", int(status));
  // This stream has been reset, abort the callback.
  if (state_ == Responded) {
    return;
  }
  if (status != Status::Ok) {
    stats_.denied_.inc();
    state_ = Responded;
    // verification failed
    Http::Code code = Http::Code::Unauthorized;
    // return failure reason as message body
    Http::Utility::sendLocalReply(false, *decoder_callbacks_, false, code,
                                  ::google::jwt_verify::getStatusString(status));
    return;
  }

  stats_.allowed_.inc();
  state_ = Complete;
  if (stopped_) {
    decoder_callbacks_->continueDecoding();
  }
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  if (state_ == Calling) {
    return Http::FilterDataStatus::StopIterationAndWatermark;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::HeaderMap&) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  if (state_ == Calling) {
    return Http::FilterTrailersStatus::StopIteration;
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  decoder_callbacks_ = &callbacks;
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
