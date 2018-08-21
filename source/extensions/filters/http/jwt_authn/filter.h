#pragma once

#include "envoy/http/filter.h"

#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"

#include "extensions/filters/http/jwt_authn/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// The Envoy filter to process JWT auth.
class Filter : public Http::StreamDecoderFilter,
               public Verifier::Callbacks,
               public Logger::Loggable<Logger::Id::filter> {
public:
  Filter(JwtAuthnFilterStats& stats, std::vector<MatcherConstSharedPtr> rule_matchers);

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
  // The state of the request
  enum State { Init, Calling, Responded, Complete };
  State state_ = Init;
  // Mark if request has been stopped.
  bool stopped_ = false;
  std::vector<MatcherConstSharedPtr> rule_matchers_;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
