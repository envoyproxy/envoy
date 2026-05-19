#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

#include <memory>
#include <string>
#include <utility>

#include "envoy/common/exception.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/router/router.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/jwt/jwt.h"
#include "source/common/jwt/status.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

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

absl::optional<envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest>
retrieveTokenRequest(Upstream::ThreadLocalCluster* cluster) {
  if (cluster == nullptr) {
    return absl::nullopt;
  }

  auto filter_metadata = cluster->info()->metadata().typed_filter_metadata();
  const auto filter_it = filter_metadata.find(std::string(FilterName));
  if (filter_it == filter_metadata.end()) {
    return absl::nullopt;
  }

  // 1. Try to unpack as the new GcpTokenRequest first.
  envoy::extensions::filters::http::gcp_authn::v3::GcpTokenRequest token_request;
  if (MessageUtil::unpackTo(filter_it->second, token_request).ok()) {
    try {
      MessageUtil::validate(token_request, ProtobufMessage::getStrictValidationVisitor());
      return token_request;
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "GcpTokenRequest validation failed: {}", e.what());
      return absl::nullopt;
    }
  }

  // 2. Fall back to unpacking the legacy Audience message and map it transparently.
  envoy::extensions::filters::http::gcp_authn::v3::Audience legacy_audience;
  if (MessageUtil::unpackTo(filter_it->second, legacy_audience).ok()) {
    token_request.mutable_jwt()->set_audience(legacy_audience.url());
    return token_request;
  }

  return absl::nullopt;
}
} // namespace

using ::Envoy::Router::RouteConstSharedPtr;
using Http::FilterHeadersStatus;
using JwtVerify::Status;

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

  auto token_request_opt = retrieveTokenRequest(cluster);

  if (token_request_opt.has_value()) {
    token_request_ = token_request_opt.value();
    if (jwt_token_cache_ != nullptr) {
      auto token = jwt_token_cache_->lookUp(token_request_);
      if (token.has_value()) {
        // If token is found in the cache, we add the token string to the request directly and
        // continue the filter chain iteration.
        addTokenToRequest(hdrs, token.value(), filter_config_->token_header());
        return FilterHeadersStatus::Continue;
      }
    }

    // Save the pointer to the request headers for header manipulation based on http response later.
    request_header_map_ = &hdrs;

    client_->fetchToken(token_request_, *this);
    initiating_call_ = false;
  } else {
    // There is no need to fetch the token if no audience is specified because no
    // authentication will be performed. So, we just continue the filter chain iteration.
    stats_.retrieve_audience_failed_.inc();
    state_ = State::Complete;
  }

  // Stop the iteration for headers as well as data and trailers for the current filter and the
  // filters following.
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
        jwt_token_cache_->insert(token_request_, std::make_unique<GcpToken>(token_val));
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
