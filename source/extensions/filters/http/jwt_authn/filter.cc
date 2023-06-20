#include "source/extensions/filters/http/jwt_authn/filter.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_split.h"
#include "jwt_verify_lib/status.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

namespace {
constexpr absl::string_view InvalidTokenErrorString = ", error=\"invalid_token\"";
constexpr uint32_t MaximumUriLength = 256;
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    access_control_request_method_handle(Http::CustomHeaders::get().AccessControlRequestMethod);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    origin_handle(Http::CustomHeaders::get().Origin);

bool isCorsPreflightRequest(const Http::RequestHeaderMap& headers) {
  return headers.getMethodValue() == Http::Headers::get().MethodValues.Options &&
         !headers.getInlineValue(origin_handle.handle()).empty() &&
         !headers.getInlineValue(access_control_request_method_handle.handle()).empty();
}

// The prefix used in the response code detail sent from jwt authn filter.
constexpr absl::string_view kRcDetailJwtAuthnPrefix = "jwt_authn_access_denied";

std::string generateRcDetails(absl::string_view error_msg) {
  // Replace space with underscore since RCDetails may be written to access log.
  // Some log processors assume each log segment is separated by whitespace.
  return absl::StrCat(kRcDetailJwtAuthnPrefix, "{", StringUtil::replaceAllEmptySpace(error_msg),
                      "}");
}

} // namespace

Filter::Filter(FilterConfigSharedPtr config)
    : stats_(config->stats()), config_(std::move(config)) {}

void Filter::onDestroy() {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  if (context_) {
    context_->cancel();
  }
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);

  state_ = Calling;
  stopped_ = false;

  if (config_->bypassCorsPreflightRequest() && isCorsPreflightRequest(headers)) {
    // The CORS preflight doesn't include user credentials, bypass regardless of JWT requirements.
    // See http://www.w3.org/TR/cors/#cross-origin-request-with-preflight.
    ENVOY_LOG(debug, "CORS preflight request bypassed regardless of JWT requirements");
    stats_.cors_preflight_bypassed_.inc();
    onComplete(Status::Ok);
    return Http::FilterHeadersStatus::Continue;
  }

  const Verifier* verifier = nullptr;
  const auto* per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<PerRouteFilterConfig>(decoder_callbacks_);
  if (per_route_config != nullptr) {
    std::string error_msg;
    std::tie(verifier, error_msg) = config_->findPerRouteVerifier(*per_route_config);
    if (!error_msg.empty()) {
      stats_.denied_.inc();
      state_ = Responded;
      decoder_callbacks_->sendLocalReply(Http::Code::Forbidden,
                                         absl::StrCat("Failed JWT authentication: ", error_msg),
                                         nullptr, absl::nullopt, generateRcDetails(error_msg));
      return Http::FilterHeadersStatus::StopIteration;
    }
  } else {
    verifier = config_->findVerifier(headers, *decoder_callbacks_->streamInfo().filterState());
  }

  if (verifier == nullptr) {
    onComplete(Status::Ok);
  } else {
    original_uri_ = Http::Utility::buildOriginalUri(headers, MaximumUriLength);
    // Verify the JWT token, onComplete() will be called when completed.
    context_ = Verifier::createContext(headers, decoder_callbacks_->activeSpan(), this);
    verifier->verify(context_);
  }

  if (state_ == Complete) {
    return Http::FilterHeadersStatus::Continue;
  }
  ENVOY_LOG(debug, "Called Filter : {} Stop", __func__);
  stopped_ = true;
  return Http::FilterHeadersStatus::StopIteration;
}

void Filter::setExtractedData(const ProtobufWkt::Struct& extracted_data) {
  decoder_callbacks_->streamInfo().setDynamicMetadata("envoy.filters.http.jwt_authn",
                                                      extracted_data);
}

void Filter::onComplete(const Status& status) {
  ENVOY_LOG(debug, "Jwt authentication completed with: {}",
            ::google::jwt_verify::getStatusString(status));
  // This stream has been reset, abort the callback.
  if (state_ == Responded) {
    return;
  }
  if (Status::Ok != status) {
    stats_.denied_.inc();
    state_ = Responded;
    // verification failed
    Http::Code code =
        status == Status::JwtAudienceNotAllowed ? Http::Code::Forbidden : Http::Code::Unauthorized;
    // return failure reason as message body
    decoder_callbacks_->sendLocalReply(
        code, ::google::jwt_verify::getStatusString(status),
        [uri = this->original_uri_, status](Http::ResponseHeaderMap& headers) {
          std::string value = absl::StrCat("Bearer realm=\"", uri, "\"");
          if (status != Status::JwtMissed) {
            absl::StrAppend(&value, InvalidTokenErrorString);
          }
          headers.setCopy(Http::Headers::get().WWWAuthenticate, value);
        },
        absl::nullopt, generateRcDetails(::google::jwt_verify::getStatusString(status)));
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

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
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
