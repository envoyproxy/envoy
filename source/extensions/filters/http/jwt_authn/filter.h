#pragma once

#include "envoy/http/filter.h"

#include "common/common/logger.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// The Envoy filter to process JWT auth.
class Filter : public Http::StreamDecoderFilter,
               public Authenticator::Callbacks,
               public Logger::Loggable<Logger::Id::filter> {
public:
  Filter(JwtAuthnFilterStats& stats, AuthenticatorPtr auth);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  // the function for Authenticator::Callbacks interface.
  // It will be called when its verify() call is completed.
  void onComplete(const ::google::jwt_verify::Status& status) override;

  // The callback funcion.
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  // The stats object.
  JwtAuthnFilterStats& stats_;
  // The auth object.
  AuthenticatorPtr auth_;
  // The state of the request
  enum State { Init, Calling, Responded, Complete };
  State state_ = Init;
  // Mark if request has been stopped.
  bool stopped_ = false;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
