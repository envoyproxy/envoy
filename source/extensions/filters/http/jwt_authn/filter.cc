#include "extensions/filters/http/jwt_authn/filter.h"

#include "common/http/utility.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

Filter::Filter(DataStoreFactorySharedPtr store_factory) : store_factory_(store_factory) {}

void Filter::onDestroy() {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  if (auth_) {
    auth_->onDestroy();
  }
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::HeaderMap& headers, bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);

  // TODO: support per-route config.
  // store_factory can be created in route::per_filter_config

  // For now, only use config from filter.
  if (!store_factory_) {
    return Http::FilterHeadersStatus::Continue;
  }
  auth_ = Authenticator::create(store_factory_);

  // Remove headers configured to pass payload
  auth_->sanitizePayloadHeaders(headers);

  state_ = Calling;
  stopped_ = false;

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
    state_ = Responded;
    // verification failed
    Http::Code code = Http::Code::Unauthorized;
    // return failure reason as message body
    Http::Utility::sendLocalReply(false, *decoder_callbacks_, false, code,
                                  ::google::jwt_verify::getStatusString(status));
    return;
  }

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
