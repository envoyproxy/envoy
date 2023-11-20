#include "source/extensions/filters/http/jwt_authn/authenticator.h"

#include "envoy/http/async_client.h"

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "absl/strings/str_split.h"
#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/struct_utils.h"
#include "jwt_verify_lib/verify.h"

using ::google::jwt_verify::CheckAudience;
using ::google::jwt_verify::Status;
using ::google::jwt_verify::StructUtils;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// If the number is unsigned 64 bit integer, convert to string as integer,
// otherwise, convert to string as double.
static std::string convertClaimDoubleToString(double double_value) {
  double int_part;
  if (double_value < 0 ||
      double_value >= static_cast<double>(std::numeric_limits<uint64_t>::max()) ||
      modf(double_value, &int_part) != 0) {
    return std::to_string(double_value);
  }
  const uint64_t int_claim_value = static_cast<uint64_t>(double_value);
  return std::to_string(int_claim_value);
}
/**
 * Object to implement Authenticator interface.
 */
class AuthenticatorImpl : public Logger::Loggable<Logger::Id::jwt>,
                          public Authenticator,
                          public Common::JwksFetcher::JwksReceiver {
public:
  AuthenticatorImpl(const CheckAudience* check_audience,
                    const absl::optional<std::string>& provider, bool allow_failed,
                    bool allow_missing, JwksCache& jwks_cache,
                    Upstream::ClusterManager& cluster_manager,
                    CreateJwksFetcherCb create_jwks_fetcher_cb, TimeSource& time_source)
      : jwks_cache_(jwks_cache), cm_(cluster_manager),
        create_jwks_fetcher_cb_(create_jwks_fetcher_cb), check_audience_(check_audience),
        provider_(provider), is_allow_failed_(allow_failed), is_allow_missing_(allow_missing),
        time_source_(time_source) {}
  // Following functions are for JwksFetcher::JwksReceiver interface
  void onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) override;
  void onJwksError(Failure reason) override;
  // Following functions are for Authenticator interface.
  void verify(Http::HeaderMap& headers, Tracing::Span& parent_span,
              std::vector<JwtLocationConstPtr>&& tokens,
              SetExtractedJwtDataCallback set_extracted_jwt_data_cb, AuthenticatorCallback callback,
              ClearRouteCacheCallback clear_route_cb) override;
  void onDestroy() override;

  TimeSource& timeSource() { return time_source_; }

private:
  // Returns the name of the authenticator. For debug logging only.
  std::string name() const;

  // Verify with a specific public key.
  void verifyKey();

  // Handle Good Jwt either Cache JWT or verified public key.
  void handleGoodJwt(bool cache_hit);

  // Normalize and set the payload metadata.
  void setPayloadMetadata(const ProtobufWkt::Struct& jwt_payload);

  // Calls the callback with status.
  void doneWithStatus(const Status& status);

  // Start verification process. It will continue to eliminate tokens with invalid claims until it
  // finds one to verify with key.
  void startVerify();

  // Copy the JWT Claim to HTTP Header. Returns true iff header is added.
  bool addJWTClaimToHeader(const std::string& claim_name, const std::string& header_name);

  // The jwks cache object.
  JwksCache& jwks_cache_;

  // the cluster manager object.
  Upstream::ClusterManager& cm_;

  // The callback used to create a JwksFetcher instance.
  CreateJwksFetcherCb create_jwks_fetcher_cb_;

  // The Jwks fetcher object
  Common::JwksFetcherPtr fetcher_;

  // The token data
  std::vector<JwtLocationConstPtr> tokens_;
  JwtLocationConstPtr curr_token_;
  // The JWT object.
  std::unique_ptr<::google::jwt_verify::Jwt> owned_jwt_;
  // The JWKS data object
  JwksCache::JwksData* jwks_data_{};
  // The HTTP request headers
  Http::HeaderMap* headers_{};
  // The active span for the request
  Tracing::Span* parent_span_{&Tracing::NullSpan::instance()};
  // The callback function called to set the extracted payload and header from a verified JWT.
  SetExtractedJwtDataCallback set_extracted_jwt_data_cb_;
  // The on_done function.
  AuthenticatorCallback callback_;
  // Clear route cache callback function.
  ClearRouteCacheCallback clear_route_cb_;
  // Set to true to clear the route cache.
  bool clear_route_cache_{false};
  // check audience object.
  const CheckAudience* check_audience_;
  // specific provider or not when it is allow missing or failed.
  const absl::optional<std::string> provider_;
  const bool is_allow_failed_;
  const bool is_allow_missing_;
  TimeSource& time_source_;
  ::google::jwt_verify::Jwt* jwt_{};
};

std::string AuthenticatorImpl::name() const {
  if (provider_) {
    return provider_.value() + (is_allow_missing_ ? "-OPTIONAL" : "");
  }
  if (is_allow_failed_) {
    return "_IS_ALLOW_FAILED_";
  }
  if (is_allow_missing_) {
    return "_IS_ALLOW_MISSING_";
  }
  return "_UNKNOWN_";
}

void AuthenticatorImpl::verify(Http::HeaderMap& headers, Tracing::Span& parent_span,
                               std::vector<JwtLocationConstPtr>&& tokens,
                               SetExtractedJwtDataCallback set_extracted_jwt_data_cb,
                               AuthenticatorCallback callback,
                               ClearRouteCacheCallback clear_route_cb) {
  ASSERT(!callback_);
  headers_ = &headers;
  parent_span_ = &parent_span;
  tokens_ = std::move(tokens);
  set_extracted_jwt_data_cb_ = std::move(set_extracted_jwt_data_cb);
  callback_ = std::move(callback);
  clear_route_cb_ = std::move(clear_route_cb);
  clear_route_cache_ = false;

  ENVOY_LOG(debug, "{}: JWT authentication starts (allow_failed={}), tokens size={}", name(),
            is_allow_failed_, tokens_.size());
  if (tokens_.empty()) {
    doneWithStatus(Status::JwtMissed);
    return;
  }

  startVerify();
}

void AuthenticatorImpl::startVerify() {
  ASSERT(!tokens_.empty());
  ENVOY_LOG(debug, "{}: startVerify: tokens size {}", name(), tokens_.size());
  curr_token_ = std::move(tokens_.back());
  tokens_.pop_back();

  bool use_jwt_cache = false;
  Status status;
  if (provider_.has_value()) {
    jwks_data_ = jwks_cache_.findByProvider(*provider_);
    jwt_ = jwks_data_->getJwtCache().lookup(curr_token_->token());
    if (jwt_ != nullptr) {
      jwks_cache_.stats().jwt_cache_hit_.inc();
      use_jwt_cache = true;
    } else {
      jwks_cache_.stats().jwt_cache_miss_.inc();
    }
  }

  if (!use_jwt_cache) {
    ENVOY_LOG(debug, "{}: Parse Jwt {}", name(), curr_token_->token());
    owned_jwt_ = std::make_unique<::google::jwt_verify::Jwt>();
    status = owned_jwt_->parseFromString(curr_token_->token());
    jwt_ = owned_jwt_.get();

    if (status != Status::Ok) {
      doneWithStatus(status);
      return;
    }
  }

  ENVOY_LOG(debug, "{}: Verifying JWT token of issuer {}", name(), jwt_->iss_);
  // Check if `iss` is allowed.
  if (!curr_token_->isIssuerAllowed(jwt_->iss_)) {
    doneWithStatus(Status::JwtUnknownIssuer);
    return;
  }

  // Issuer is configured
  if (!provider_) {
    jwks_data_ = jwks_cache_.findByIssuer(jwt_->iss_);
  }
  // When `provider` is valid, findByProvider should never return nullptr.
  // Only when `allow_missing` or `allow_failed` is used, `provider` is invalid,
  // and this authenticator is checking tokens from all providers. In this case,
  // Jwt `iss` field is used to find the first provider with the issuer.
  // If not found, use the first provider without issuer specified.
  // If still no found, fail the request with UnknownIssuer error.
  if (!jwks_data_) {
    doneWithStatus(Status::JwtUnknownIssuer);
    return;
  }

  // Default is 60 seconds
  uint64_t clock_skew_seconds = ::google::jwt_verify::kClockSkewInSecond;
  if (jwks_data_->getJwtProvider().clock_skew_seconds() > 0) {
    clock_skew_seconds = jwks_data_->getJwtProvider().clock_skew_seconds();
  }
  const uint64_t unix_timestamp = DateUtil::nowToSeconds(timeSource());
  status = jwt_->verifyTimeConstraint(unix_timestamp, clock_skew_seconds);
  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }

  // Check if audience is allowed
  const bool is_allowed = check_audience_ ? check_audience_->areAudiencesAllowed(jwt_->audiences_)
                                          : jwks_data_->areAudiencesAllowed(jwt_->audiences_);
  if (!is_allowed) {
    doneWithStatus(Status::JwtAudienceNotAllowed);
    return;
  }

  if (use_jwt_cache) {
    handleGoodJwt(/*cache_hit=*/true);
    return;
  }

  auto jwks_obj = jwks_data_->getJwksObj();
  if (jwks_obj != nullptr && !jwks_data_->isExpired()) {
    // TODO(qiwzhang): It would seem there's a window of error whereby if the JWT issuer
    // has started signing with a new key that's not in our cache, then the
    // verification will fail even though the JWT is valid. A simple fix
    // would be to check the JWS kid header field; if present check we have
    // the key cached, if we do proceed to verify else try a new JWKS retrieval.
    // JWTs without a kid header field in the JWS we might be best to get each
    // time? This all only matters for remote JWKS.

    verifyKey();
    return;
  }

  // TODO(potatop): potential optimization.
  // Only one remote jwks will be fetched, verify will not continue until it is completed. This is
  // fine for provider name requirements, as each provider has only one issuer, but for allow
  // missing or failed there can be more than one issuers. This can be optimized; the same remote
  // jwks fetching can be shared by two requests.
  if (jwks_data_->getJwtProvider().has_remote_jwks()) {
    if (!fetcher_) {
      fetcher_ = create_jwks_fetcher_cb_(cm_, jwks_data_->getJwtProvider().remote_jwks());
    }
    fetcher_->fetch(*parent_span_, *this);
    return;
  }
  // No valid keys for this issuer. This may happen as a result of incorrect local
  // JWKS configuration.
  doneWithStatus(Status::JwksNoValidKeys);
}

void AuthenticatorImpl::onJwksSuccess(google::jwt_verify::JwksPtr&& jwks) {
  jwks_cache_.stats().jwks_fetch_success_.inc();
  const Status status = jwks_data_->setRemoteJwks(std::move(jwks))->getStatus();
  if (status != Status::Ok) {
    doneWithStatus(status);
  } else {
    verifyKey();
  }
}

void AuthenticatorImpl::onJwksError(Failure) {
  jwks_cache_.stats().jwks_fetch_failed_.inc();
  doneWithStatus(Status::JwksFetchFail);
}

void AuthenticatorImpl::onDestroy() {
  if (fetcher_) {
    fetcher_->cancel();
  }
}

// Verify with a specific public key.
void AuthenticatorImpl::verifyKey() {
  const Status status =
      ::google::jwt_verify::verifyJwtWithoutTimeChecking(*jwt_, *jwks_data_->getJwksObj());

  if (status != Status::Ok) {
    doneWithStatus(status);
    return;
  }
  handleGoodJwt(/*cache_hit=*/false);
}

bool AuthenticatorImpl::addJWTClaimToHeader(const std::string& claim_name,
                                            const std::string& header_name) {
  StructUtils payload_getter(jwt_->payload_pb_);
  const ProtobufWkt::Value* claim_value;
  const auto status = payload_getter.GetValue(claim_name, claim_value);
  std::string str_claim_value;
  if (status == StructUtils::OK) {
    switch (claim_value->kind_case()) {
    case Envoy::ProtobufWkt::Value::kStringValue:
      str_claim_value = claim_value->string_value();
      break;
    case Envoy::ProtobufWkt::Value::kNumberValue:
      str_claim_value = convertClaimDoubleToString(claim_value->number_value());
      break;
    case Envoy::ProtobufWkt::Value::kBoolValue:
      str_claim_value = claim_value->bool_value() ? "true" : "false";
      break;
    case Envoy::ProtobufWkt::Value::kStructValue:
      ABSL_FALLTHROUGH_INTENDED;
    case Envoy::ProtobufWkt::Value::kListValue: {
      std::string output;
      auto status = claim_value->has_struct_value()
                        ? ProtobufUtil::MessageToJsonString(claim_value->struct_value(), &output)
                        : ProtobufUtil::MessageToJsonString(claim_value->list_value(), &output);
      if (status.ok()) {
        str_claim_value = Envoy::Base64::encode(output.data(), output.size());
      }
      break;
    }
    default:
      ENVOY_LOG(debug, "[jwt_auth] claim : {} is of an unknown type '{}'", claim_name,
                claim_value->kind_case());
      break;
    }

    if (!str_claim_value.empty()) {
      headers_->addCopy(Http::LowerCaseString(header_name), str_claim_value);
      ENVOY_LOG(debug, "[jwt_auth] claim : {} with value : {} is added to the header : {}",
                claim_name, str_claim_value, header_name);
      return true;
    }
  }
  return false;
}

void AuthenticatorImpl::handleGoodJwt(bool cache_hit) {
  // Forward the payload
  const auto& provider = jwks_data_->getJwtProvider();

  if (!provider.forward_payload_header().empty()) {
    if (provider.pad_forward_payload_header()) {
      std::string payload_with_padding = jwt_->payload_str_base64url_;
      Base64::completePadding(payload_with_padding);
      headers_->addCopy(Http::LowerCaseString(provider.forward_payload_header()),
                        payload_with_padding);
    } else {
      headers_->addCopy(Http::LowerCaseString(provider.forward_payload_header()),
                        jwt_->payload_str_base64url_);
    }
  }

  // Copy JWT claim to header
  bool header_added = false;
  for (const auto& header_and_claim : provider.claim_to_headers()) {
    header_added |=
        addJWTClaimToHeader(header_and_claim.claim_name(), header_and_claim.header_name());
  }
  if (provider.clear_route_cache() && header_added) {
    clear_route_cache_ = true;
  }

  if (!provider.forward()) {
    // TODO(potatop) remove JWT from queries.
    // Remove JWT from headers.
    curr_token_->removeJwt(*headers_);
  }

  if (set_extracted_jwt_data_cb_) {
    if (!provider.header_in_metadata().empty()) {
      set_extracted_jwt_data_cb_(provider.header_in_metadata(), jwt_->header_pb_);
    }
    if (!provider.payload_in_metadata().empty()) {
      setPayloadMetadata(jwt_->payload_pb_);
    }
  }
  if (provider_ && !cache_hit) {
    // move the ownership of "owned_jwt_" into the function.
    jwks_data_->getJwtCache().insert(curr_token_->token(), std::move(owned_jwt_));
  }
  doneWithStatus(Status::Ok);
}

void AuthenticatorImpl::setPayloadMetadata(const ProtobufWkt::Struct& jwt_payload) {
  const auto& provider = jwks_data_->getJwtProvider();
  const auto& normalize = provider.normalize_payload_in_metadata();
  if (normalize.space_delimited_claims().size() == 0) {
    set_extracted_jwt_data_cb_(provider.payload_in_metadata(), jwt_payload);
  }
  // Make a temporary copy to normalize the JWT struct.
  ProtobufWkt::Struct out_payload = jwt_payload;
  for (const auto& claim : normalize.space_delimited_claims()) {
    const auto& it = jwt_payload.fields().find(claim);
    if (it != jwt_payload.fields().end() && it->second.has_string_value()) {
      const auto list = absl::StrSplit(it->second.string_value(), ' ', absl::SkipEmpty());
      for (const auto& elt : list) {
        (*out_payload.mutable_fields())[claim].mutable_list_value()->add_values()->set_string_value(
            elt);
      }
    }
  }
  set_extracted_jwt_data_cb_(provider.payload_in_metadata(), out_payload);
}

void AuthenticatorImpl::doneWithStatus(const Status& status) {
  ENVOY_LOG(debug, "{}: JWT token verification completed with: {}", name(),
            ::google::jwt_verify::getStatusString(status));

  if (Status::Ok != status) {
    // Forward the failed status to dynamic metadata
    ENVOY_LOG(debug, "status is: {}", ::google::jwt_verify::getStatusString(status));

    std::string failed_status_in_metadata;

    if (jwks_data_) {
      failed_status_in_metadata = jwks_data_->getJwtProvider().failed_status_in_metadata();
    } else if (jwks_cache_.getSingleProvider()) {
      failed_status_in_metadata =
          jwks_cache_.getSingleProvider()->getJwtProvider().failed_status_in_metadata();
    }

    if (!failed_status_in_metadata.empty()) {

      ProtobufWkt::Struct failed_status;
      auto& failed_status_fields = *failed_status.mutable_fields();
      failed_status_fields["code"].set_number_value(enumToInt(status));
      failed_status_fields["message"].set_string_value(google::jwt_verify::getStatusString(status));
      ENVOY_LOG(debug, "Code: {} Message: {}", enumToInt(status),
                google::jwt_verify::getStatusString(status));
      set_extracted_jwt_data_cb_(failed_status_in_metadata, failed_status);
    }
  }

  // If a request has multiple tokens, all of them must be valid. Otherwise it may have
  // following security hole: a request has a good token and a bad one, it will pass
  // verification, forwarded to the backend, and the backend may mistakenly use the bad
  // token as the good one that passed the verification.

  // Unless allowing failed or missing, all tokens must be verified successfully.
  if ((Status::Ok != status && !is_allow_failed_ && !is_allow_missing_) || tokens_.empty()) {
    tokens_.clear();
    if (is_allow_failed_) {
      callback_(Status::Ok);
    } else if (is_allow_missing_ && status == Status::JwtMissed) {
      callback_(Status::Ok);
    } else {
      callback_(status);
    }
    callback_ = nullptr;

    if (clear_route_cache_ && clear_route_cb_) {
      clear_route_cb_();
    }
    clear_route_cb_ = nullptr;

    return;
  }

  startVerify();
}

} // namespace

AuthenticatorPtr Authenticator::create(const CheckAudience* check_audience,
                                       const absl::optional<std::string>& provider,
                                       bool allow_failed, bool allow_missing, JwksCache& jwks_cache,
                                       Upstream::ClusterManager& cluster_manager,
                                       CreateJwksFetcherCb create_jwks_fetcher_cb,
                                       TimeSource& time_source) {
  return std::make_unique<AuthenticatorImpl>(check_audience, provider, allow_failed, allow_missing,
                                             jwks_cache, cluster_manager, create_jwks_fetcher_cb,
                                             time_source);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
