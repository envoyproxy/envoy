#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/status.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {
void addTokenToRequest(Http::RequestHeaderMap& hdrs, absl::string_view token_str,
                       const envoy::extensions::filters::http::gcp_authn::v3::TokenHeader& header) {
  if (header.ByteSizeLong() == 0) {
    std::string id_token = absl::StrCat("Bearer ", token_str);
    hdrs.setCopy(authorizationHeaderKey(), id_token);
  } else {
    std::string id_token = absl::StrCat(header.value_prefix(), token_str);
    hdrs.setCopy(Http::LowerCaseString(header.name()), id_token);
  }
}

std::optional<envoy::extensions::filters::http::gcp_authn::v3::Audience>
retrieveAudience(Upstream::ThreadLocalCluster* cluster) {
  if (cluster == nullptr) {
    return std::nullopt;
  }

  auto filter_metadata = cluster->info()->metadata().typed_filter_metadata();
  const auto filter_it = filter_metadata.find(std::string(FilterName));
  if (filter_it == filter_metadata.end()) {
    return std::nullopt;
  }

  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  if (MessageUtil::unpackTo(filter_it->second, audience).ok()) {
    return audience;
  }

  return std::nullopt;
}
} // namespace

using ::Envoy::Router::RouteConstSharedPtr;
using Http::FilterHeadersStatus;
using JwtVerify::Status;

std::optional<std::string>
GcpAuthnFilter::getClientCertFingerprint(Upstream::ThreadLocalCluster* cluster) {
  if (cluster == nullptr) {
    return std::nullopt;
  }

  auto match_data = cluster->info()->transportSocketMatcher().resolve(nullptr, nullptr);
  auto& factory = match_data.factory_;
  auto client_context_config_opt = factory.clientContextConfig();
  if (!client_context_config_opt.has_value()) {
    return std::nullopt;
  }

  const Ssl::ClientContextConfig& client_context_config = client_context_config_opt.value();
  const auto tls_certs = client_context_config.tlsCertificates();
  if (tls_certs.empty()) {
    return std::nullopt;
  }

  const auto& cert_config = tls_certs[0].get();
  const std::string& cert_pem = cert_config.certificateChain();
  if (cert_pem.empty()) {
    return std::nullopt;
  }

  auto fingerprint_or_error = cert_fingerprinter_->getFingerprintFromPem(cert_pem);
  if (!fingerprint_or_error.ok()) {
    ENVOY_LOG(warn, "Failed to calculate certificate fingerprint: {}",
              fingerprint_or_error.status().message());
    return std::nullopt;
  }

  stats_.client_cert_fingerprint_calculated_.inc();
  return fingerprint_or_error.value();
}

// TODO(tyxia) Handle the duplicated outstanding requests.
Http::FilterHeadersStatus GcpAuthnFilter::decodeHeaders(Http::RequestHeaderMap& hdrs, bool) {
  const auto route = decoder_callbacks_->route();
  if (!route || !route->routeEntry()) {
    // Nothing to do if no route, continue the filter chain iteration.
    return Envoy::Http::FilterHeadersStatus::Continue;
  }

  state_ = State::Calling;
  initiating_call_ = true;

  Envoy::Upstream::ThreadLocalCluster* cluster =
      context_.serverFactoryContext().clusterManager().getThreadLocalCluster(
          route->routeEntry()->clusterName());

  auto audience_opt = retrieveAudience(cluster);
  if (!audience_opt.has_value()) {
    stats_.retrieve_audience_failed_.inc();
    state_ = State::Complete;
    return FilterHeadersStatus::Continue;
  }

  audience_ = audience_opt.value();

  // Resolve fingerprint if bound token is requested. Note client_cert_fingerprint_ remains
  // std::nullopt by default for unbound tokens.
  if (audience_.has_bound_jwt() || audience_.has_bound_access_token()) {
    client_cert_fingerprint_ = getClientCertFingerprint(cluster);
    if (!client_cert_fingerprint_.has_value()) {
      ENVOY_LOG(warn,
                "Failed to fetch bound token: client certificate fingerprint is unavailable.");
      state_ = State::Complete;
      decoder_callbacks_->sendLocalReply(
          Http::Code::InternalServerError,
          "Failed to fetch bound token: client certificate fingerprint is unavailable.", nullptr,
          std::nullopt, "bound_token_fingerprint_unavailable");
      return FilterHeadersStatus::StopAllIterationAndWatermark;
    }
  }

  // Check cache first and reuse previously fetched token if possible.
  if (jwt_token_cache_ != nullptr) {
    auto token = jwt_token_cache_->lookUp(audience_, client_cert_fingerprint_);
    if (token.has_value()) {
      addTokenToRequest(hdrs, token.value(), filter_config_->token_header());
      state_ = State::Complete;
      return FilterHeadersStatus::Continue;
    }
  }

  request_header_map_ = &hdrs;

  // Execute token-type specific fetch calls.
  if (audience_.has_bound_access_token()) {
    client_->fetchBoundAccessToken(audience_, client_cert_fingerprint_.value(), *this);
  } else if (audience_.has_bound_jwt()) {
    client_->fetchBoundJwt(audience_, client_cert_fingerprint_.value(), *this);
  } else if (audience_.has_access_token()) {
    client_->fetchUnboundAccessToken(audience_, *this);
  } else if (!audience_.url().empty()) {
    client_->fetchUnboundJwt(audience_, *this);
  } else {
    ENVOY_LOG(warn, "Audience is configured but no token is specified, continuing without token.");
    stats_.empty_audience_.inc();
    state_ = State::Complete;
    return FilterHeadersStatus::Continue;
  }

  initiating_call_ = false;
  return state_ == State::Complete ? FilterHeadersStatus::Continue
                                   : Http::FilterHeadersStatus::StopAllIterationAndWatermark;
}

void GcpAuthnFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

void GcpAuthnFilter::onComplete(absl::StatusOr<GcpToken> token) {
  state_ = State::Complete;
  if (!initiating_call_) {
    if (token.ok()) {
      // Modify the request header to include the ID token in a header (by default, the
      // `Authorization: Bearer ID_TOKEN` header).
      GcpToken token_val = *token;
      if (request_header_map_ != nullptr) {
        addTokenToRequest(*request_header_map_, token_val.token, filter_config_->token_header());
      } else {
        ENVOY_LOG(debug, "No request header to be modified.");
      }
      if (jwt_token_cache_ != nullptr) {
        // Insert the token into cache along with the ownership transfer.
        jwt_token_cache_->insert(std::make_unique<GcpToken>(token_val));
      }
    } else {
      ENVOY_LOG(error, "Failed to fetch token: {}", token.status().message());
    }
    decoder_callbacks_->continueDecoding();
  }
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
